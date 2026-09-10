#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
mutex_priority_donate.c

Demonstrates priority inheritance: without it, a low priority task holding the 
mutex with a medium-priority task that never touches the mutex can starve a high 
priority task waiting for the mutex indefinitely, because the scheduler never lets 
the low priority mutex run to finishing using the mutex and unlock it. With inheritance, 
the low-priority owner is temporarily boosted to the waiting high priority task's priority, 
so the owner can finish and unlock.

Tasks:
  task1 "H", priority 1 (highest)
  task2 "M", priority 2
  task3 "L", priority 3 (lowest)

Since H and M are higher priority than L, both delays on entry to stage the 
scenario to let L grab the mutex first.

Sequence:
    1. L runs while H and M are still delaying, locks mtx, and starts a long busy_spin 
        to simulate holding it while doing some work.
    2. t=50 M wakes, preempts L (M's priority 2 beats L's 3), and enters its 
        print + busy_spin loop. M never blocks again after this, so it would run 
        forever and L would never get the CPU back on its own.
    3. t=100 H wakes, preempts M, and calls os_mutex_lock(&mtx, OS_WAIT_FOREVER).
        mtx is still held by L, so H blocks and because H's priority (1) beats L's
        current effective priority (3), L's effective priority is boosted to 1.
    4. That boost breaks the inversion: L (now priority 1) outranks M (priority 2), 
        so the scheduler runs L instead of M. L resumes its busy_spin exactly where 
        it was preempted and eventually finishes it.
    5. L unlocks mtx. Ownership hands off to H, L's effective priority reverts to
        its base (3). H becomes both the new owner and the highest priority ready 
        task, so the scheduler switches straight to H. 
    6. H acquires the mutex and halts in while(1)

Expected ITM/SWO output: 
Task schedular initialized
Task L: locking mutex
Task L: mutex acquired
Task M: running
Task H: locking mutex       <- H wakes at t=100, waits for mutex, boosts L to priority 1
Task L: unlocking mutex     <- L, now boosted, finally gets to finish and unlock
Task H: mutex acquired

Output without priority inheritance would be:
Task schedular initialized
Task L: locking mutex
Task L: mutex acquired
Task M: running
Task H: locking mutex
Task M: running
Task M: running
Task M: running
... (keeps repeating "Task M: running" forever)


*/

#define SPIN_BETWEEN_PRINTS 500000u // bigger = slower prints; tune for a readable ITM rate

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void task1_handler(void);
void task2_handler(void);
void task3_handler(void);

void busy_spin(uint32_t iterations);

uint32_t task1_stack[1024] __attribute__((aligned(8)));
uint32_t task2_stack[1024] __attribute__((aligned(8)));
uint32_t task3_stack[1024] __attribute__((aligned(8)));

mutex_t mtx;

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(task1_handler, 1, task1_stack, sizeof(task1_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(task2_handler, 2, task2_stack, sizeof(task2_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(task3_handler, 3, task3_stack, sizeof(task3_stack)) != OS_OK)
        error_hanlder();

    if (os_mutex_create(&mtx) != OS_OK)
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

void task1_handler(void)
{
    os_task_delay(100); // let task 3 to lock the mutex first
    while (1)
    {
        printf("Task H: locking mutex\n");
        os_err_t lock_result = os_mutex_lock(&mtx, OS_WAIT_FOREVER);
        if (lock_result == OS_OK)
        {
            printf("Task H: mutex acquired\n");
            while(1); // sample app finishes here
        } 
        else 
        {
            // should not get here
            printf("Task H: mutex lock failed\n");
            error_hanlder();
        }
    }
}
void task2_handler(void)
{
    os_task_delay(50); // let task 3 to lock the mutex first
    while (1)
    {
        printf("Task M: running\n");
        busy_spin(SPIN_BETWEEN_PRINTS); 
    }
}
void task3_handler(void)
{
    while (1)
    {
        printf("Task L: locking mutex\n");
        os_err_t lock_result = os_mutex_lock(&mtx, 1000);
        if (lock_result == OS_OK)
        {
            printf("Task L: mutex acquired\n");
            busy_spin(SPIN_BETWEEN_PRINTS * 5); // simulate some work while holding the mutex
            printf("Task L: unlocking mutex\n");
            if (os_mutex_unlock(&mtx) == OS_OK)
            {
                // should not get here since task 1 has higher priority and 
                // should be scheduled as it now owns the mutex
                printf("Task L: mutex unlocked\n");
                error_hanlder();
            }
            else
            {
                // should not get here
                printf("Task L: mutex unlock failed\n");
                error_hanlder();
            }
        }
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
