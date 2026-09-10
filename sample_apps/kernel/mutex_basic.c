#include <stdint.h>
#include <stdio.h>

#include "kortos.h"

/*
mutex_basic.c

Demonstrates the basic features of mutex: create, lock, recursive-lock 
rejection, not owner unlock rejection, sucessful lock that blocks and 
is handed off to the waiter, and lock timeout.

Tasks:
    task1, priority 1, action: lock -> recursive lock -> delay(50) ->
                                    unlock -> delay(50) -> lock again
    task2, priority 2, action: unlock -> lock(1000) -> delay(200)

Flow:
    1. task1: os_mutex_lock(&mtx, 100) -> OS_OK  "mutex acquired"
        - task1 is higher priority, so it runs first
    2. task1: loops immediately, locks again while still owner -> OS_ERR_MTX_RECURSIVE_LOCK "recursive lock"
    3. task1: os_task_delay(50) -> yields with the mutex still held
    4. task2: os_mutex_unlock(&mtx) -> OS_ERR_MTX_NOT_OWNER "mutex unlock failed, not owner"
        - task2 is not the owner (task1 locked it and is owner) so cannot unlock it
    5. task2: os_mutex_lock(&mtx, 1000) -> blocks, waiting for task1 to unlock
        - task2 is now in the mutex's wait list, and will be unblocked when task1 unlocks the mutex
    6. task1: os_mutex_unlock(&mtx) -> OS_OK "mutex unlocked", ownership is handed to task2
        - task1's 50 ticks delay passed and since it has higher priority it immediately get scheduled
    7. task1: os_task_delay(50) -> yields again so task2 can run
    8. task2: os_mutex_lock(&mtx, 1000) finally unblocks -> OS_OK  "mutex acquired"
        - the 1000 ticks timeout has not passed yet, so when task1 unlocks the mutex
        task2 is removed from the waitlist and now owns the mutex
    9. task2: os_task_delay(200) -> holds the mutex for 200 ticks
    10. task1: wakes up after its second 50 tick delay and loops to lock the mutex again
    11. task1: os_mutex_lock(&mtx, 100) -> OS_ERR_UNAVAILABLE "mutex lock timed out", 
        - timeout of 100 ticks expires before task2's 200 tick hold ends
    12. task1: while(1) -> halts, end of the demo

Expected ITM/SWO output:
Task schedular initialized
Task 1: mutex acquired
Task 1: recursive lock
Task 2: mutex unlock failed, not owner
Task 1: mutex unlocked
Task 2: mutex acquired
Task 1: mutex lock timed out

- "recursive lock" instead of a deadlock or a second OS_OK proves a task cannot 
  lock a mutex it already owns.
- "mutex unlock failed, not owner" proves a task cannot release a mutex it does not own.
- "Task 2: mutex acquired" appearing only after "Task 1: mutex unlocked" proves 
  that a successful os_mutex_lock() blocks, and that ownership transfers to the waiter
  when its unblocked.
- "mutex lock timed out" proves a blocked os_mutex_lock() gives up once its timeout 
  elapses and returns OS_ERR_UNAVAILABLE , instead of blocking forever.

*/

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void task1_handler(void);
void task2_handler(void);
void task3_handler(void);

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

    if (os_mutex_create(&mtx) != OS_OK)
        error_hanlder();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

void task1_handler(void)
{
    while (1)
    {
        // slow down prints so they are readable in ITM viewer, but the task is still running and not blocked
        // busy_spin(SPIN_BETWEEN_PRINTS);
        os_err_t lock_result = os_mutex_lock(&mtx, 100);
        if (lock_result == OS_OK)
        {
            printf("Task 1: mutex acquired\n");
        }
        else if (lock_result == OS_ERR_MTX_RECURSIVE_LOCK)
        {
            printf("Task 1: recursive lock\n");
            os_task_delay(50);
            // unlock the mutex so that task 2 can acquire it
            if (os_mutex_unlock(&mtx) == OS_OK)
            {
                printf("Task 1: mutex unlocked\n");
            }
            else 
            {
                // should not get here
                printf("Task 1: mutex unlock failed\n");
                error_hanlder();
            }
            os_task_delay(50);
        }
        else if (lock_result == OS_ERR_UNAVAILABLE)
        {
            printf("Task 1: mutex lock timed out\n");
            while(1); // sample app finishes here
        }
    }
}

void task2_handler(void)
{
    while (1)
    {
        os_err_t unlock_result = os_mutex_unlock(&mtx);
        if (unlock_result == OS_ERR_MTX_NOT_OWNER)
        {
            printf("Task 2: mutex unlock failed, not owner\n");
        }
        else
        {
            // should not get here 
            printf("Task 2: mutex unlocked\n");
            error_hanlder();
        }

        os_err_t lock_result = os_mutex_lock(&mtx, 1000);
        if (lock_result == OS_OK)
        {
            printf("Task 2: mutex acquired\n");
            os_task_delay(200);
        }
        else if (lock_result == OS_ERR_MTX_RECURSIVE_LOCK)
        {
            // should not get here
            printf("Task 2: recursive lock\n");
            error_hanlder();
        }
        else if (lock_result == OS_ERR_UNAVAILABLE)
        {
            // should not get here
            printf("Task 2: mutex lock timed out\n");
            error_hanlder();
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
