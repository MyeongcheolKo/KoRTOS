#include <stdint.h>
#include <stdio.h>

#include "kortos.h"


/*
queue_basic.c

Demonstrates message queue with a blocking producer and consumer. 
Items arrive in FIFO order (id 0, 1, 2, ...), producer blocks once 
the queue fills up, producer wakes once the consumer frees a slot, 
and data integrity where each received value matches its id. 

Tasks:
    producer_handler, priority 1 (highest), sends 10 messages, 1000 tick timeout
    consumer_handler, priority 2, receives 10 messages, then delay(50) between each

Sequence: 
Producer sends ids 0-4 uninterrupted, filling the QUEUE_SIZE(5) queue. Its
id=5 send finds the queue full and blocks. It yields, and consumer finally gets
a chance to run. Consumer's recv() runs, copies the first msg with id=0 item passed in,
checks the send waitlist and sees producer is waiting, so it yields and wakes producer. 
So the producer preempts consumer before consumer's first printf, "Consumer: received 
message id=0, data=0" runs. Producer now running, copies id=5 message into index=0, 
prints "Producer: message sent id=5, data=50", moves on to id=6, and prints 
"Producer: sending message id=6, data=60" and blocks again since the queue is full again.
Now consumer resumes and finally prints "Consumer: received message id=0, data=0". 
So the prints show producer stays one send ahead, and each "received" printf only appears 
once producer's next send blocks. This repeats for every message until the producer finish
all sends. 

Sample output:
Task schedular initialized
Producer: sending message id=0, data=0
Producer: message sent id=0, data=0
Producer: sending message id=1, data=10
Producer: message sent id=1, data=10
Producer: sending message id=2, data=20
Producer: message sent id=2, data=20
Producer: sending message id=3, data=30
Producer: message sent id=3, data=30
Producer: sending message id=4, data=40
Producer: message sent id=4, data=40
Producer: sending message id=5, data=50
Producer: message sent id=5, data=50
Producer: sending message id=6, data=60
Consumer: received message id=0, data=0
Producer: message sent id=6, data=60
Producer: sending message id=7, data=70
Consumer: received message id=1, data=10
Producer: message sent id=7, data=70
Producer: sending message id=8, data=80
Consumer: received message id=2, data=20
Producer: message sent id=8, data=80
Producer: sending message id=9, data=90
Consumer: received message id=3, data=30
Producer: message sent id=9, data=90
Producer: finished
Consumer: received message id=4, data=40
Consumer: received message id=5, data=50
Consumer: received message id=6, data=60
Consumer: received message id=7, data=70
Consumer: received message id=8, data=80
Consumer: received message id=9, data=90
Consumer: finished

*/

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void producer_handler(void);
void consumer_handler(void);

uint32_t task1_stack[1024] __attribute__((aligned(8)));
uint32_t task2_stack[1024] __attribute__((aligned(8)));

queue_t queue;

#define QUEUE_SIZE 5

typedef struct {
    uint8_t id;
    uint16_t data;
} message_t;

message_t queue_buffer[QUEUE_SIZE]; 

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    if (os_task_create(producer_handler, 1, task1_stack, sizeof(task1_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(consumer_handler, 2, task2_stack, sizeof(task2_stack)) != OS_OK)
        error_hanlder();

    if (os_queue_create(&queue, queue_buffer, sizeof(message_t), QUEUE_SIZE, PRIORITY) != OS_OK)
        error_hanlder();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }
}

void producer_handler(void)
{
    while (1)
    {
        for (int i = 0; i < 10; i++)
        {
            message_t msg = { .id = (uint8_t)i, .data = (uint16_t)(i * 10) };
            printf("Producer: sending message id=%d, data=%d\n", msg.id, msg.data);
            if (os_queue_send_from_task(&queue, &msg, 1000) == OS_OK)
            {
                printf("Producer: message sent id=%d, data=%d\n", msg.id, msg.data);
            }
            else 
            {
                // should not get here
                printf("Producer: queue send failed\n");
                error_hanlder();
            }
        }
        printf("Producer: finished\n");
        os_task_delay(OS_WAIT_FOREVER); // block forever after sending all messages
    }
}

void consumer_handler(void)
{
    while (1)
    {
        for (int i = 0; i < 10; i++)
        {
            message_t msg;    
            if (os_queue_recv_from_task(&queue, &msg, 1000) == OS_OK)
            {
                printf("Consumer: received message id=%d, data=%d\n", msg.id, msg.data);
            }
            else 
            {
                // should not get here
                printf("Consumer: queue receive failed\n");
                error_hanlder();
            }

            os_task_delay(50); // simulate slow processing
        }
        printf("Consumer: finished\n");
        while(1);
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
