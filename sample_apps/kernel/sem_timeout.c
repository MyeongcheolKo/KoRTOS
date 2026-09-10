#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
sem_timeout.c

Demonstrates the kernel's semaphore timeout behavior: a task blocked on
os_sem_wait() unblocks on its own once its timeout expires, instead of
blocking forever, and os_sem_wait() reports that as OS_SEM_UNAVAILABLE.

Tasks:
  task1, priority 1, os_sem_wait(&sem, 500)  -> highest priority, times out first
  task2, priority 2, os_sem_wait(&sem, 1000) -> times out second
  task3, priority 3, os_sem_wait(&sem, 1500) -> lowest priority, times out last

sem is created with initial count 0 (os_sem_create(&sem, 0, FIFO)) and is
never posted anywhere in this app. That means every os_sem_wait() call is
guaranteed to time out, there is no path where the semaphore is actually
acquired.

Each task waits with a different timeout (500, 1000, 1500 ticks), so the three "timed out"
messages appear staggered rather than together, proving each task's wakeup_tick is
tracked and expired independently rather than sharing one global timeout.

Expected ITM/SWO output (repeats):
Task schedular initialized
This is task 1                          <- highest priority (1) runs first
This is task 2
This is task 3
Task 1: semaphore wait timed out        <- task1's 500 tick timeout fires first
This is task 1                             
Task 1: semaphore wait timed out        <- task1 cycles again 500 ticks later
This is task 1
Task 2: semaphore wait timed out        <- task2's 1000 tick timeout fires
This is task 2
Task 1: semaphore wait timed out        <- task1 cycles a 3rd time
This is task 1
Task 3: semaphore wait timed out        <- task3's 1500 tick timeout finally fires
This is task 3
Task 1: semaphore wait timed out        <- task1 cycles again
This is task 1
  ...

- every wait ends in "semaphore wait timed out", never "semaphore acquired",
  proving the timeout path fires correctly when the semaphore is never posted.
- task1 shows up more often than task2 (1000 tick timeout) and task3 (1500 tick timeout) 
  since its timeout is shortest(500 tick timeout), proving wakeup_tick is tracked 
  independently per task rather than off one shared timer.
- task1 also benefits from being highest priority, whenever it and a lower
  priority task become READY on the same tick, task1 preempts and prints
  first, so task2/task3 only print once task1 is blocked again.

NOTE: printf is neither atomic nor reentrant, and the kernel is preemptive, so 
the expected output is just what a typical cycle looks like, the exact order can 
shift slightly run to run, since printf's own execution time isn't perfectly constant
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

semaphore_t sem;

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

    if (os_sem_create(&sem, 0, FIFO) != OS_OK)
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
    while (1)
    {
        printf("This is task 1\n");
        // slow down prints so they are readable in ITM viewer, but the task is still running and not blocked
        busy_spin(SPIN_BETWEEN_PRINTS);
        if (os_sem_wait(&sem, 500) == OS_SEM_UNAVAILABLE)
        {
            printf("Task 1: semaphore wait timed out\n");
        }
        else
        {
            // should not get here
            printf("Task 1: semaphore acquired\n");
            while (1);
        }
    }
}
void task2_handler(void)
{
    while (1)
    {
        printf("This is task 2 \n");
        // slow down prints so they are readable in ITM viewer, but the task is still running and not blocked
        busy_spin(SPIN_BETWEEN_PRINTS);
        if (os_sem_wait(&sem, 1000) == OS_SEM_UNAVAILABLE)
        {
            printf("Task 2: semaphore wait timed out\n");
        }
        else
        {
            // should not get here
            printf("Task 2: semaphore acquired\n");
            while (1);
        }
    }
}
void task3_handler(void)
{
    while (1)
    {
        printf("This is task 3\n");
        // slow down prints so they are readable in ITM viewer, but the task is still running and not blocked
        busy_spin(SPIN_BETWEEN_PRINTS);
        if (os_sem_wait(&sem, 1500) == OS_SEM_UNAVAILABLE)
        {
            printf("Task 3: semaphore wait timed out\n");
        }
        else
        {
            // should not get here
            printf("Task 3: semaphore acquired\n");
            while (1);
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
