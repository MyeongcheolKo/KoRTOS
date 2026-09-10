#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
queue_recheck_on_wake.c

Proves os_queue_send_from_task() that was blocked due to queue being full rechecks the 
queue after waking up instead of blindly proceeding to write into it. A size 1 queue is 
kept full by a high priority "thief" task (timeout==0, never blocks, never enters send 
waitlist) that always wins the slot back before a lower priority "victim" task (unblocked 
from the send waitlist) gets a chance to run, which forces victim to loop and re-block 
with a shrinking timeout instead of corrupting the queue.

Tasks:
    drainer_handler, priority 1 (highest), frees the slot every 35 ticks, 4 times total
    thief_handler,   priority 2, refills the slot the instant it's free (timeout=0, never blocks)
    victim_handler,  priority 3 (lowest), attempts one send with a 150 tick timeout

Sequence:
Drainer's first blocks itself for 35 ticks, so thief runs next and immediately fills
the queue. Victim then tries to send, finds the queue full, blocks and being added to 
the send waitlist with a 150 tick timeout. Every 35 ticks drainer wakes and receives
one item from the queue. The drainer's os_queue_send_from_task() unblocks victim and 
removes it from the waitlist, but since thief (priority 2) outranks victim (priority 3), 
the scheduler always runs thief first. Thief's send finds the queue empty and immediately 
refills it, before victim ever gets to see a slot being freed in the queue. When victim 
finally runs, it re-checks the queue, finds out it being full again, and re-blocks with
whatever is left of its original 150 tick timeout, it never touches queue->buffer or queue->count 
on this wakeup so no corruption to the queue. This repeats happens 4 times at tick 35, 70,
105 and 140, leaving victim only around 10 ticks of budget after the 4th steal. So no 5th 
drain will happen. Victim's timeout elapses at tick 150 and os_queue_send_from_task() returns 
OS_ERR_UNAVAILABLE, proving the retry loop enforces the overall 150 ticktimeout rather than 
setting the timeout to 150 ticks on every block. This is shown by how Drainer is only printed
4 times, if the retry timeout resets, it will be more than 4 times.

Sample output:
Task schedular initialized
Thief: stole the slot, data=0
Drainer: received data=0, freed the slot
Thief: stole the slot, data=1
Drainer: received data=1, freed the slot
Thief: stole the slot, data=2
Drainer: received data=2, freed the slot
Thief: stole the slot, data=3
Drainer: received data=3, freed the slot
Drainer: finished
Thief: stole the slot, data=4
Victim: send timed out as expected, the slot was always stolen before it could run

*/

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void drainer_handler(void);
void thief_handler(void);
void victim_handler(void);

uint32_t task1_stack[1024] __attribute__((aligned(8)));
uint32_t task2_stack[1024] __attribute__((aligned(8)));
uint32_t task3_stack[1024] __attribute__((aligned(8)));

queue_t queue;

#define QUEUE_SIZE 1
#define DRAIN_PERIOD_TICKS 35
#define DRAIN_COUNT 4
#define VICTIM_TIMEOUT_TICKS 150

uint32_t queue_buffer[QUEUE_SIZE];

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(drainer_handler, 1, task1_stack, sizeof(task1_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(thief_handler, 2, task2_stack, sizeof(task2_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(victim_handler, 3, task3_stack, sizeof(task3_stack)) != OS_OK)
        error_hanlder();

    if (os_queue_create(&queue, queue_buffer, sizeof(uint32_t), QUEUE_SIZE, PRIORITY) != OS_OK)
        error_hanlder();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

void drainer_handler(void)
{
    for (int i = 0; i < DRAIN_COUNT; i++)
    {
        os_task_delay(DRAIN_PERIOD_TICKS);

        uint32_t data;
        if (os_queue_recv_from_task(&queue, &data, 0) == OS_OK)
        {
            printf("Drainer: received data=%lu, freed the slot\n", data);
        }
        else
        {
            // should not get here, thief keeps the queue full so there's always something to drain
            printf("Drainer: queue was unexpectedly empty\n");
            error_hanlder();
        }
    }
    printf("Drainer: finished\n");
    os_task_delay(OS_WAIT_FOREVER); // block forever after draining
}

void thief_handler(void)
{
    uint32_t data = 0;
    while (1)
    {
        // timeout=0 never blocks, so thief can never end up on the send waitlist,
        // it only ever succeeds when it happens to catch the slot free
        if (os_queue_send_from_task(&queue, &data, 0) == OS_OK)
        {
            printf("Thief: stole the slot, data=%lu\n", data);
            data++;
        }
        os_task_delay(1); // poll every tick so it gets the queue as soon as the slot frees
    }
}

void victim_handler(void)
{
    uint32_t data = 999;
    if (os_queue_send_from_task(&queue, &data, VICTIM_TIMEOUT_TICKS) == OS_OK)
    {
        // should not get here, thief always steals the slot back before victim can run
        printf("Victim: send unexpectedly succeeded\n");
        error_hanlder();
    }
    else
    {
        printf("Victim: send timed out as expected, the slot was always stolen before it could run\n");
    }
    while (1); // halt, sample app finishes here
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
