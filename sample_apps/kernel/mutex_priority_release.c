#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
mutex_priority_release.c

Demonstrates that releasing one waiter's priority boost does not stomp another,
unrelated waiter's boost. task L owns two independent mutexes
directly, with a different waiter blocked on each. L isn't blocked on anything
itself.

Tasks:
    taskL, priority 4 (lowest) 
    taskM, priority 2 
    taskH, priority 1 (highest)
    taskP, priority 3

Sequence:
    1. M, L, and H all delay on entry to stage the scenario so L can grab both mutexes first.
    1. L locks mtx1 then mtx2, then busy_spins a long time to simulate holding 
        both while working.
    2. M wakes and locks on mtx2 (OS_WAIT_FOREVER), boosts L to priority 2.
    3. P wakes but L (now priority 2) beats P (3), so P should not print at all.
    4. H wakes and blocks on mtx1 with a short timeout, boosts L to priority 1 temporarily.
    5. H's timeout fires before L's busy_spin ends. H is removed from mtx1's waitlist,
        triggering a recompute of L's priority. Since L still owns mtx2 with M waiting on it, 
        L must rescan all of its owned mutexes and land back on priority 2, not fall through 
        to its base priority 4. If recompute only checks mtx1 (the mutex that just changed)
        instead of every mutex L owns, it would drop L straight to 4, letting P (priority 3) 
        to sneak in.
    6. L finishes its busy_spin, unlocks mtx2. Now L holds no mutex so it drops to its base 
        priority 4.
    7. M aquires mtx2 and immediately unlocks it and yields. 
    7. Now since L and M both yielded, P preempts L and starts printing.

Expected ITM/SWO output:
Task schedular initialized
Task L: locking mtx1
Task L: mtx1 acquired
Task L: locking mtx2
Task L: mtx2 acquired
Task M: locking mtx2          <- blocks, boosts L to priority 2
Task H: locking mtx1          <- blocks, boosts L to priority 1
Task H: mtx1 lock timed out   <- H gives up, recompute must drop L to 2, not 4
                              <- no "Task P: running" here
Task L: unlocking mtx2
Task M: mtx2 acquired
Task M: unlocking mtx2
Task L: unlocking mtx1
Task P: running               <- P is finally free
Task P: running
... ("Task P: running" repeats)

Broken output (recompute only rescans the mutex that changed, not every mutex the
task owns):
...
Task H: mtx1 lock timed out
Task P: running            <- BUG: M is still waiting on mtx2, but L got
Task P: running                dropped all the way to its base priority
...

*/

#define SPIN_BETWEEN_PRINTS 500000u // bigger = slower prints; tune for a readable ITM rate
#define H_TIMEOUT 100               // ticks; must stay well shorter than L's busy_spin hold below so H reliably times out

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void taskL_handler(void);
void taskM_handler(void);
void taskH_handler(void);
void taskP_handler(void);

void busy_spin(uint32_t iterations);

uint32_t taskH_stack[1024] __attribute__((aligned(8)));
uint32_t taskL_stack[1024] __attribute__((aligned(8)));
uint32_t taskP_stack[1024] __attribute__((aligned(8)));
uint32_t taskM_stack[1024] __attribute__((aligned(8)));

mutex_t mtx1;
mutex_t mtx2;

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(taskL_handler, 4, taskL_stack, sizeof(taskL_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskM_handler, 2, taskM_stack, sizeof(taskM_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskH_handler, 1, taskH_stack, sizeof(taskH_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(taskP_handler, 3, taskP_stack, sizeof(taskP_stack)) != OS_OK)
        error_hanlder();

    if (os_mutex_create(&mtx1) != OS_OK)
        error_hanlder();
    if (os_mutex_create(&mtx2) != OS_OK)
        error_hanlder();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

// burns CPU WITHOUT blocking, so the caller stays READY (this is the key difference from os_task_delay)
void busy_spin(uint32_t iterations)
{
    for (volatile uint32_t k = 0; k < iterations; k++)
    {
    }
}

void taskL_handler(void)
{
    printf("Task L: locking mtx1\n");
    if (os_mutex_lock(&mtx1, OS_WAIT_FOREVER) != OS_OK)
    {
        // should not get here
        printf("Task L: mtx1 lock failed\n");
        error_hanlder();
    }
    printf("Task L: mtx1 acquired\n");

    printf("Task L: locking mtx2\n");
    if (os_mutex_lock(&mtx2, OS_WAIT_FOREVER) != OS_OK)
    {
        // should not get here
        printf("Task L: mtx2 lock failed\n");
        error_hanlder();
    }
    printf("Task L: mtx2 acquired\n");

    busy_spin(SPIN_BETWEEN_PRINTS * 8); // long hold; must outlast H_TIMEOUT

    printf("Task L: unlocking mtx2\n");
    if (os_mutex_unlock(&mtx2) != OS_OK)
    {
        // should not get here
        printf("Task L: mtx2 unlock failed\n");
        error_hanlder();
    }

    printf("Task L: unlocking mtx1\n");
    if (os_mutex_unlock(&mtx1) != OS_OK)
    {
        // should not get here
        printf("Task L: mtx1 unlock failed\n");
        error_hanlder();
    }

    os_task_delay(OS_WAIT_FOREVER);
}

void taskM_handler(void)
{
    os_task_delay(20); // let L lock both mutexes first

    printf("Task M: locking mtx2\n");
    if (os_mutex_lock(&mtx2, OS_WAIT_FOREVER) != OS_OK)
    {
        // should not get here
        printf("Task M: mtx2 lock failed\n");
        error_hanlder();
    }
    printf("Task M: mtx2 acquired\n");

    printf("Task M: unlocking mtx2\n");
    if (os_mutex_unlock(&mtx2) != OS_OK)
    {
        // should not get here
        printf("Task M: mtx2 unlock failed\n");
        error_hanlder();
    }

    os_task_delay(OS_WAIT_FOREVER);
}

void taskH_handler(void)
{
    os_task_delay(60); // let M and P get going first

    printf("Task H: locking mtx1\n");
    os_err_t lock_result = os_mutex_lock(&mtx1, H_TIMEOUT);
    if (lock_result == OS_ERR_UNAVAILABLE)
    {
        printf("Task H: mtx1 lock timed out\n");
    }
    else
    {
        // should not get here
        printf("Task H: lock failed\n");
        error_hanlder();
    }

    os_task_delay(OS_WAIT_FOREVER); // watch for "Task P: running" to stay silent until L fully finishes
}

void taskP_handler(void)
{
    os_task_delay(40); // let M queue up on mtx2 before P starts competing for the CPU

    while (1)
    {
        printf("Task P: running\n");
        busy_spin(SPIN_BETWEEN_PRINTS);
    }
}

void error_hanlder(void)
{
    while (1);
}

void enable_processor_faults(void)
{
    uint32_t *p_SHCSR = (uint32_t *)0xE000ED24;
    *p_SHCSR |= (1 << 18); // usage fault
    *p_SHCSR |= (1 << 17); // bus fault
    *p_SHCSR |= (1 << 16); // mem fault
}

void HardFault_Handler(void)
{
    printf("hard fault\n");
    while (1);
}

void MemManage_Handler(void)
{
    printf("mem fault\n");
    while (1);
}

void BusFault_Handler(void)
{
    printf("bus fault\n");
    while (1);
}

void UsageFault_Handler(void)
{
    printf("usage fault\n");
    while (1);
}
