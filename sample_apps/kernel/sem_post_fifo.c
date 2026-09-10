#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
sem_post_fifo.c

Demonstrates os_sem_post() with a FIFO semaphore: a task blocked in
os_sem_wait() is woken by another task posting the semaphore, and the wait
returns OS_OK instead of timing out. With FIFO, each post releases whichever
task has been waiting longest, regardless of task priority.

See sem_post_priority.c for the same mechanism with PRIORITY unblocking, where
the wait list is drained by task priority instead of arrival order.

Tasks:
  consumer1, priority 1, os_sem_wait(&sem, 5000) -> blocks until posted
  consumer2, priority 2, os_sem_wait(&sem, 5000) -> blocks until posted
  producer,  priority 3, os_task_delay(1000) then os_sem_post(&sem)

sem is created with initial count 0 (os_sem_create(&sem, 0, FIFO)), so both
consumers block immediately at startup and sit in the semaphore's wait list.
The producer is the lowest priority task on purpose: it only reaches its first
os_task_delay() after both consumers have already queued up, so the wait list
is populated before any post happens.

The consumers' 5000 tick timeout is 5x the producer's 1000 tick post period, so
a post always arrives long before the timeout could expire. A "TIMED OUT" line
in the output would therefore mean something is broken, not a normal outcome.

Observed ITM/SWO output (repeats):
Task schedular initialized
Consumer 1: waiting on semaphore     <- highest priority, so it queues first
Consumer 2: waiting on semaphore
Producer: posting semaphore          <- fires after its 1000 tick delay
Consumer 1: semaphore acquired       <- FIFO released the head of the wait list
Consumer 1: waiting on semaphore     <- re-queues, now behind consumer2
Producer: posting semaphore
Consumer 2: semaphore acquired       <- consumer2 is at the head now
Consumer 2: waiting on semaphore
Producer: posting semaphore
Consumer 1: semaphore acquired
Consumer 1: waiting on semaphore
  ...

- every wait ends in "semaphore acquired", never "TIMED OUT", proving the post
  path unblocks a waiter and that os_sem_wait() reports OS_OK for it.
- the two consumers alternate rather than one starving, proving each post
  releases exactly one waiter and that FIFO releases them in the order they
  blocked.
- consumer1 outranks consumer2 but still has to take its turn. That is the
  whole point of FIFO: release order is a property of the wait list, not of
  task priority. Under PRIORITY unblocking consumer1 would win every post and
  consumer2 would starve.

NOTE: unlike round_robin_priority.c, this app never has two tasks runnable at
the same time (a consumer only wakes when the producer posts, and the producer
prints before posting), so printf output does not braid together here even
though printf is not atomic or reentrant.
*/

#define POST_PERIOD_TICKS 1000u        // producer sleeps this long between posts
#define CONSUMER_WAIT_TIMEOUT 5000u    // generously longer than POST_PERIOD_TICKS, should never fire

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_handler(void);

void consumer1_handler(void);
void consumer2_handler(void);
void producer_handler(void);

uint32_t consumer1_stack[1024] __attribute__((aligned(8)));
uint32_t consumer2_stack[1024] __attribute__((aligned(8)));
uint32_t producer_stack[1024] __attribute__((aligned(8)));

semaphore_t sem;

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(consumer1_handler, 1, consumer1_stack, sizeof(consumer1_stack)) != OS_OK)
        error_handler();
    if (os_task_create(consumer2_handler, 2, consumer2_stack, sizeof(consumer2_stack)) != OS_OK)
        error_handler();
    if (os_task_create(producer_handler, 3, producer_stack, sizeof(producer_stack)) != OS_OK)
        error_handler();

    if (os_sem_create(&sem, 0, FIFO) != OS_OK)
        error_handler();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

void consumer1_handler(void)
{
    while (1)
    {
        printf("Consumer 1: waiting on semaphore\n");
        if (os_sem_wait(&sem, CONSUMER_WAIT_TIMEOUT) == OS_OK)
        {
            printf("Consumer 1: semaphore acquired\n");
        }
        else
        {
            // the producer posts every 1000 ticks, so a 5000 tick wait should never expire
            printf("Consumer 1: TIMED OUT (unexpected)\n");
        }
    }
}
void consumer2_handler(void)
{
    while (1)
    {
        printf("Consumer 2: waiting on semaphore\n");
        if (os_sem_wait(&sem, CONSUMER_WAIT_TIMEOUT) == OS_OK)
        {
            printf("Consumer 2: semaphore acquired\n");
        }
        else
        {
            // the producer posts every 1000 ticks, so a 5000 tick wait should never expire
            printf("Consumer 2: TIMED OUT (unexpected)\n");
        }
    }
}
void producer_handler(void)
{
    while (1)
    {
        os_task_delay(POST_PERIOD_TICKS);
        // print before posting: the post makes a higher priority consumer READY, so
        // printing after it risks being preempted mid printf and braiding the output
        printf("Producer: posting semaphore\n");
        os_sem_post(&sem);
    }
}

void error_handler(void)
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
