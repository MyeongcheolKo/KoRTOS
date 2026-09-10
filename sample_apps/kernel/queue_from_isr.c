#include <stdint.h>
#include <stdio.h>

#include "kortos.h"
#include "GPIO.h"


/*
queue_from_isr.c

Demonstrates os_queue_send_from_isr() and os_queue_recv_from_isr() together from a real
external interrupt (EXTI15_10, B1 button on PC13 for the STM32F446RE deb board). 

Tasks:
    producer_handler, priority 1 (highest), pushes an incrementing sample into sensor_queue
                                            ISR receives the sample from sensor_queue and
                                            sends it to event_queue
    consumer_handler, priority 2, blocks on event_queue and prints the sample when the button 
                                    is pressed to send message to event_queue via the ISR.

Queues:
    sensor_queue, size 1. producer_handler pushes an incrementing sample into it every 
    SAMPLE_PERIOD_TICKS ticks via os_queue_send_from_task(), non-blocking (timeout=0). 
    If the ISR hasn't drained the previous sample yet, the send fails and that new
    sample is skipped, the old successful send sample preserves.

    event_queue, size 4, holds a button_event_t. The button ISR drains sensor_queue with
    os_queue_recv_from_isr() and forwards the result via os_queue_send_from_isr() to 
    consumer_handler, which blocks on os_queue_recv_from_task().

Sample output:
Task schedular initialized
Producer: sample=0 ready
Consumer: press #1, sample=0
Producer: sample=471 ready
Consumer: press #2, sample=471
Producer: sample=1017 ready
Consumer: press #3, sample=1017
Producer: sample=1202 ready
Consumer: press #4, sample=1202
Producer: sample=1854 ready
Consumer: press #5, sample=1854
Producer: sample=2594 ready
Consumer: press #6, sample=2594
Producer: sample=2748 ready
Consumer: press #7, sample=2748
Producer: sample=3071 ready
Consumer: press #8, sample=3071
Producer: sample=3610 ready

Why this proves send_from_isr/recv_from_isr work:
Each "Consumer: press #N" sample exactly matches the "Producer: ... ready" line printed
just before it (0->0, 471->471, 1017->1017, ...), so os_queue_recv_from_isr() is always
draining the value actually sitting in sensor_queue, never stale or corrupted data. The
big jumps between ready values (0->471->1017) are expected: sensor_queue is size 1, so
os_queue_send_from_task(timeout=0) only succeeds right after a press drains it, and fails
silently every SAMPLE_PERIOD_TICKS in between while the counter keeps climbing.
*/

// enables UsageFault, BusFault, and MemManageFault so they trap as their own exceptions instead of escalating to HardFault
void enable_processor_faults(void);

void error_hanlder(void);

void producer_handler(void);
void consumer_handler(void);

uint32_t task1_stack[1024] __attribute__((aligned(8)));
uint32_t task2_stack[1024] __attribute__((aligned(8)));

queue_t sensor_queue;
queue_t event_queue;

#define SENSOR_QUEUE_SIZE 1
#define EVENT_QUEUE_SIZE 4
#define SAMPLE_PERIOD_TICKS 10

typedef struct {
    uint32_t press_count;
    uint32_t sample;
    uint8_t has_sample; // 0 if sensor_queue was empty when the ISR drained it
} button_event_t;

uint32_t sensor_queue_buffer[SENSOR_QUEUE_SIZE];
button_event_t event_queue_buffer[EVENT_QUEUE_SIZE];

int main(void)
{
    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    enable_processor_faults();

    GPIO_Handle_t GPIO_button;
    GPIO_button.p_GPIOx = GPIOC; // B1 button is connected to PC13
    GPIO_button.GPIO_config.GPIO_pin_num = GPIO_PIN_NO_13;
    GPIO_button.GPIO_config.GPIO_pin_mode = GPIO_MODE_IT_FT; // falling edge trigger
    GPIO_button.GPIO_config.GPIO_pin_pupd = GPIO_PIN_PU;
    GPIO_clock_control(GPIO_button.p_GPIOx, ENABLE);
    GPIO_init(&GPIO_button);

    GPIO_set_priority(IRQ_NO_EXTI15_10, 10);
    GPIO_IRQ_config(IRQ_NO_EXTI15_10, ENABLE); // button is PC13 so EXTI13 which is within 10-15

    if (os_task_create(producer_handler, 1, task1_stack, sizeof(task1_stack)) != OS_OK)
        error_hanlder();
    if (os_task_create(consumer_handler, 2, task2_stack, sizeof(task2_stack)) != OS_OK)
        error_hanlder();

    if (os_queue_create(&sensor_queue, sensor_queue_buffer, sizeof(uint32_t), SENSOR_QUEUE_SIZE, PRIORITY) != OS_OK)
        error_hanlder();
    if (os_queue_create(&event_queue, event_queue_buffer, sizeof(button_event_t), EVENT_QUEUE_SIZE, PRIORITY) != OS_OK)
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
    uint32_t sample = 0;
    while (1)
    {
        // timeout=0, so if the button ISR hasn't drained the previous sample, skip this one
        // rather than blocking the producer on a slow consumer
        if (os_queue_send_from_task(&sensor_queue, &sample, 0) == OS_OK)
        {
            printf("Producer: sample=%lu ready\n", sample);
        }
        sample++;
        os_task_delay(SAMPLE_PERIOD_TICKS);
    }
}

void consumer_handler(void)
{
    while (1)
    {
        button_event_t evt;
        if (os_queue_recv_from_task(&event_queue, &evt, OS_WAIT_FOREVER) == OS_OK)
        {
            if (evt.has_sample)
            {
                printf("Consumer: press #%lu, sample=%lu\n", evt.press_count, evt.sample);
            }
            else
            {
                printf("Consumer: press #%lu, no new sample since last press\n", evt.press_count);
            }
        }
    }
}

void EXTI15_10_IRQHandler(void)
{
    static uint32_t press_count = 0;

    button_event_t evt;
    evt.press_count = ++press_count;
    evt.has_sample = (os_queue_recv_from_isr(&sensor_queue, &evt.sample) == OS_OK);

    os_queue_send_from_isr(&event_queue, &evt); // dropped_count tracks this if event_queue is ever full

    GPIO_IRQ_handler(GPIO_PIN_NO_13); // clear the EXTI pending bit for this pin
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
