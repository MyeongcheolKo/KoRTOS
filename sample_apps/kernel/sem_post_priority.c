#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
sem_post_priority.c

Demonstrates os_sem_post() with a PRIORITY semaphore: each post scans the wait
list and releases the highest priority waiter, regardless of how long anyone
has been waiting.

See sem_post_fifo.c for the same mechanism with FIFO unblocking, where the wait
list is drained in arrival order instead.

Tasks:
  consumer_high, priority 1, os_task_delay(300) then os_sem_wait(&sem, 5000)
  consumer_mid,  priority 2, os_task_delay(200) then os_sem_wait(&sem, 5000)
  consumer_low,  priority 3, os_task_delay(100) then os_sem_wait(&sem, 5000)
  producer,      priority 4, os_task_delay(500) then os_sem_post(&sem) x3

The staggered entry delays are the whole trick. Left alone, the scheduler runs
the highest priority task first, so consumers would queue up in priority order
and a FIFO semaphore and a PRIORITY semaphore would produce identical output,
proving nothing. The delays invert that: consumer_low reaches os_sem_wait()
first (t=100), then consumer_mid (t=200), then consumer_high (t=300), so the
wait list ends up in the exact reverse of priority order:

  wait list by arrival:  [low, mid, high]
  released by PRIORITY:   high, mid, low   <- reversed, so ordering came from
                                              priority and not from arrival

The producer waits 500 ticks so all three consumers are queued before it posts,
then posts three times back to back to drain the whole list in one go. Because
os_sem_post() yields, each post switches to the task it just released before
the next post runs, so the releases show up in the output one at a time.

The consumers' 5000 tick timeout is far longer than the ~400 ticks any of them
actually waits, so a "TIMED OUT" line means something is broken.

Expected ITM/SWO output (repeats):
Task schedular initialized
Consumer LOW (priority 3): waiting on semaphore     <- queues FIRST
Consumer MID (priority 2): waiting on semaphore
Consumer HIGH (priority 1): waiting on semaphore    <- queues LAST
Producer: posting semaphore 3 times
Consumer HIGH (priority 1): semaphore acquired      <- released FIRST
Consumer MID (priority 2): semaphore acquired
Consumer LOW (priority 3): semaphore acquired       <- released LAST
Consumer LOW (priority 3): waiting on semaphore     <- cycles back to wait again
Consumer MID (priority 2): waiting on semaphore
Consumer HIGH (priority 1): waiting on semaphore
Producer: posting semaphore 3 times
  ...

- the release order(HIGH, MID, LOW) is the exact reverse of the arrival order 
  (LOW, MID, HIGH), which only a priority scan can produce. Under FIFO this same 
  app would acquire semaphores in the order LOW, MID, HIGH instead, the same order 
  as the arrival order.
- three posts release exactly three waiters, one each, proving os_sem_post()
  releases a single task per call rather than draining or broadcasting.
- consumer_low is not starved here only because the producer posts as many
  times as there are waiters. If it posted once per round, PRIORITY would hand
  every post to consumer_high and consumer_low, consumer_mid would never run, which 
  is the starvation tradeoff FIFO avoids.

NOTE: like sem_post_fifo.c, this app never has two tasks runnable at the same
time (consumers only wake when posted or when their delay expires, and the
producer prints before posting), so printf output does not braid together even
though printf is not atomic or reentrant.
*/

#define PRODUCER_GATHER_TICKS 500u  // long enough for all three consumers to queue up first
#define HIGH_ENTRY_DELAY 300u       // consumer_high queues LAST despite being highest priority
#define MID_ENTRY_DELAY 200u
#define LOW_ENTRY_DELAY 100u        // consumer_low queues FIRST despite being lowest priority
#define CONSUMER_WAIT_TIMEOUT 5000u // far longer than the ~400 ticks anyone actually waits

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_handler(void);

void consumer_high_handler(void);
void consumer_mid_handler(void);
void consumer_low_handler(void);
void producer_handler(void);

uint32_t consumer_high_stack[1024] __attribute__((aligned(8)));
uint32_t consumer_mid_stack[1024] __attribute__((aligned(8)));
uint32_t consumer_low_stack[1024] __attribute__((aligned(8)));
uint32_t producer_stack[1024] __attribute__((aligned(8)));

semaphore_t sem;

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(consumer_high_handler, 1, consumer_high_stack, sizeof(consumer_high_stack)) != OS_OK)
        error_handler();
    if (os_task_create(consumer_mid_handler, 2, consumer_mid_stack, sizeof(consumer_mid_stack)) != OS_OK)
        error_handler();
    if (os_task_create(consumer_low_handler, 3, consumer_low_stack, sizeof(consumer_low_stack)) != OS_OK)
        error_handler();
    if (os_task_create(producer_handler, 4, producer_stack, sizeof(producer_stack)) != OS_OK)
        error_handler();

    if (os_sem_create(&sem, 0, PRIORITY) != OS_OK)
        error_handler();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

void consumer_high_handler(void)
{
    while (1)
    {
        // delays the longest, so it is the LAST one into the wait list
        os_task_delay(HIGH_ENTRY_DELAY);
        printf("Consumer HIGH (priority 1): waiting on semaphore\n");
        if (os_sem_wait(&sem, CONSUMER_WAIT_TIMEOUT) == OS_OK)
        {
            printf("Consumer HIGH (priority 1): semaphore acquired\n");
        }
        else
        {
            printf("Consumer HIGH (priority 1): TIMED OUT (unexpected)\n");
        }
    }
}
void consumer_mid_handler(void)
{
    while (1)
    {
        os_task_delay(MID_ENTRY_DELAY);
        printf("Consumer MID (priority 2): waiting on semaphore\n");
        if (os_sem_wait(&sem, CONSUMER_WAIT_TIMEOUT) == OS_OK)
        {
            printf("Consumer MID (priority 2): semaphore acquired\n");
        }
        else
        {
            printf("Consumer MID (priority 2): TIMED OUT (unexpected)\n");
        }
    }
}
void consumer_low_handler(void)
{
    while (1)
    {
        // delays the least, so it is the FIRST one into the wait list
        os_task_delay(LOW_ENTRY_DELAY);
        printf("Consumer LOW (priority 3): waiting on semaphore\n");
        if (os_sem_wait(&sem, CONSUMER_WAIT_TIMEOUT) == OS_OK)
        {
            printf("Consumer LOW (priority 3): semaphore acquired\n");
        }
        else
        {
            printf("Consumer LOW (priority 3): TIMED OUT (unexpected)\n");
        }
    }
}
void producer_handler(void)
{
    while (1)
    {
        os_task_delay(PRODUCER_GATHER_TICKS);
        // print before posting: the post makes a higher priority consumer READY, so
        // printing after it risks being preempted mid printf and braiding the output
        printf("Producer: posting semaphore 3 times\n");
        // one post per queued consumer, so the whole wait list drains in priority order
        os_sem_post(&sem);
        os_sem_post(&sem);
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
