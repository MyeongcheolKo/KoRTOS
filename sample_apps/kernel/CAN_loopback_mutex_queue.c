#include <stdint.h>
#include <stdio.h>

#include "GPIO.h"
#include "CAN.h"

#include "kortos.h"

/*
CAN_loopback_mutex_queue.c

Demonstrates CAN driver integration with the kernel's mutex and queue primitives,
and makes unbounded priority inversion observable.

CAN1 runs in loopback mode, so every frame transmitted is received back through the
CAN1_RX0 interrupt, decoded by the driver, and pushed onto a queue from the ISR.

Tasks:
    rx_task,          priority 1, action: blocks on the RX queue forever, prints each frame
    control_task,     priority 2, action: locks the TX mutex, sends id 0x100, unlocks, delay(200)
    interfering_task, priority 3, action: burns CPU, touches no CAN resource and no mutex
    telemetry_task,   priority 4, action: locks the TX mutex, spins, sends id 0x200, unlocks, delay(50)

RX path: CAN1_RX0 ISR -> CAN_rx_callback -> os_queue_send_from_isr -> rx_task
TX path: os_mutex_lock -> CAN_transmit -> os_mutex_unlock

The inversion: telemetry_task holds the TX mutex across a deliberately long critical section 
with busy_spin. control_task outranks it and blocks on that mutex, while interfering_task sits
between them in priority and preempts telemetry_task without ever touching the mutex.
    
    - with inheritance: telemetry_task is boosted to control_task's priority for the
                        duration of the critical section, so interfering_task cannot
                        preempt it and control_task's wait stays bounded by that section
    - with no inheritance: interfering_task preempts telemetry_task while it holds the mutex,
                            so control_task's wait grows with interfering_task's workload

Expected output, repeating steady state:
    Task schedular initialized
    Rx task: received message with ID 0x100, DLC 4, data 0xAA 0xAA
    Control task: sent 0x100, released CAN TX mutex
    Rx task: received message with ID 0x200, DLC 2, data 0xBB 0xBB
    Telemetry task: sent 0x200, released CAN TX mutex

Each frame is received before its sender reports sending it. That ordering is correct,
since CAN_transmit polls until the frame has gone out, and in loopback it comes
straight back, so the RX interrupt fires and unblocks rx_task while the sender is still
inside its critical section. rx_task outranks both senders, so it preempts, prints, and
blocks again before the sender reaches its own unlock and print.

No "mutex lock error" line should ever appear. Both senders lock with OS_WAIT_FOREVER,
so a lock can only fail for a structural reason (recursive lock, full waitlist), never
for contention, however long telemetry_task holds the mutex.

Lines can splice into each other, since newlib's printf is not reentrant and three tasks
call it concurrently. That is a stdout artifact, not a kernel fault.
*/

// widen telemetry_task's critical section so control_task reliably contends for the mutex
#define TELEMETRY_CRITICAL_SECTION_SPIN 1000000
// interfering_task's CPU burn, raise this to make the inversion more pronounced
#define INTERFERING_WORKLOAD_SPIN 50000

void error_handler(void);

void rx_task_handler(void);
void control_task_handler(void);
void interfering_task_handler(void);
void telemetry_task_handler(void);

void busy_spin(uint32_t iterations);

uint32_t rx_task_stack[1024] __attribute__((aligned(8)));
uint32_t control_task_stack[1024] __attribute__((aligned(8)));
uint32_t interfering_task_stack[1024] __attribute__((aligned(8)));
uint32_t telemetry_task_stack[1024] __attribute__((aligned(8)));

CAN_frame_t CAN_rx_buffer[10]; 
queue_t CAN_rx_queue;

mutex_t CAN_tx_mutex;

CAN_handle_t CAN_Handle;

int main(void)
{
    // CAN Rx, PA11
    GPIO_Handle_t GPIO_CAN_Rx;
    GPIO_CAN_Rx.p_GPIOx = GPIOA;
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_num = GPIO_PIN_NO_11;
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_mode = GPIO_MODE_ALTFUNC;
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_speed = GPIO_OUT_SPEED_FAST; // dont matter for input
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_out_type = GPIO_OUT_TYPE_PP; // dont matter for input
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_pupd = GPIO_PIN_PU;          // dont matter for loopback, but at the transition before entering normal mode, loopback might be not active, so the pin is needed for the required 11 consucutive recessive bits
    GPIO_CAN_Rx.GPIO_config.GPIO_pin_alt_fcn_mode = 9;            // AF9 for CAN1_RX
    GPIO_clock_control(GPIO_CAN_Rx.p_GPIOx, ENABLE);
    GPIO_init(&GPIO_CAN_Rx);

    // CAN Tx, PA12 (dont matter for loopback)
    GPIO_Handle_t GPIO_CAN_Tx;
    GPIO_CAN_Tx.p_GPIOx = GPIOA;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_num = GPIO_PIN_NO_12;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_mode = GPIO_MODE_ALTFUNC;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_speed = GPIO_OUT_SPEED_FAST;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_out_type = GPIO_OUT_TYPE_PP;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_pupd = GPIO_PIN_NO_PUPD;
    GPIO_CAN_Tx.GPIO_config.GPIO_pin_alt_fcn_mode = 9; // AF9 for CAN1_TX
    GPIO_clock_control(GPIO_CAN_Tx.p_GPIOx, ENABLE);
    GPIO_init(&GPIO_CAN_Tx);

    // initialize the CAN peripheral
    CAN_Handle.CANx = CAN1;
    /* configure bit timing to be 500kbit/s
    APB1 clock = 16MHz
    CAN bit time = 1 / 500kHz = 2us
    APB1 / (prescaler + 1) = 1 / tq
    tq = 2us / 16 Mhz = 125ns
    prescaler = tq * APB1 - 1 = 125ns * 16MHz - 1 = 2 - 1 = 1
    CAN bit time = sync_seg + TS1 + TS2 = 16tq
    sample point at 87.5% = (1 + TS1) / 16, TS1 = 13, TS2 = 2
    */
    CAN_Handle.can_config.baud_rate_prescaler = 1;
    CAN_Handle.can_config.ts1 = 12;
    CAN_Handle.can_config.ts2 = 1;
    CAN_Handle.can_config.sjw = 0;
    CAN_Handle.can_config.mode = CAN_MODE_LOOPBACK; // loopback mode for testing
    CAN_Handle.can_config.ttcm = DISABLE;           // time triggered communication mode disabled
    if (CAN_init(&CAN_Handle) != KHAL_OK)
    {
        printf("CAN1 failed to initialize\n");
        while (1);
    }

    // configure filter to accept all messages, filter bank 0, FIFO 0
    CAN_filter_config_t filter_config;
    filter_config.bank_num = 0;
    filter_config.filter_mode = FILTER_MODE_MASK;
    filter_config.filter_scale = FILTER_SCALE_32BIT;
    filter_config.fifo_assignment = FIFO0;
    // set filter bank mask to be all dont care, so all messages are accepted
    filter_config.fr1 = 0;
    filter_config.fr2 = 0;
    if (CAN_configure_filter(&filter_config) != KHAL_OK)
    {
        printf("CAN1 failed to configure filter\n");
        while (1);
    }

    // enable interrupts for CAN1 RX FIFO 0 and set its priority
    CAN_IRQ_control(IRQ_NO_CAN1_RX0, ENABLE);
    CAN_IRQ_priority_config(IRQ_NO_CAN1_RX0, 5);

    // keep the debugger awake when the core is sleeping, so can still debug while the core is sleeping
    *(volatile uint32_t *)0xE0042004 |= (1 << 0); // STM32F4: DBGMCU_CR @ 0xE0042004, bit0 DBG_SLEEP, bit1 DBG_STOP, bit2 DBG_STANDBY

    if (os_task_create(rx_task_handler, 1, rx_task_stack, sizeof(rx_task_stack)) != OS_OK)
        error_handler();
    if (os_task_create(control_task_handler, 2, control_task_stack, sizeof(control_task_stack)) != OS_OK)
        error_handler();
    if (os_task_create(interfering_task_handler, 3, interfering_task_stack, sizeof(interfering_task_stack)) != OS_OK)
        error_handler();
    if (os_task_create(telemetry_task_handler, 4, telemetry_task_stack, sizeof(telemetry_task_stack)) != OS_OK)
        error_handler();

    if (os_mutex_create(&CAN_tx_mutex) != OS_OK)
        error_handler();
    
    if (os_queue_create(&CAN_rx_queue, CAN_rx_buffer, sizeof(CAN_frame_t), 10, PRIORITY) != OS_OK)
        error_handler();

    printf("Task schedular initialized\n");

    os_kernel_start();

    while (1)
    {
        // should never get here
    }

    return 0;
}

void rx_task_handler(void)
{
    CAN_frame_t rx_frame;

    while (1)
    {
        // wait for the queue get filled by the CAN RX interrupt handler
        os_err_t error = os_queue_recv_from_task(&CAN_rx_queue, &rx_frame, OS_WAIT_FOREVER);

        if (error == OS_OK)
        {
            printf("Rx task: received message with ID 0x%lX, DLC %d, data 0x%02X 0x%02X\n", 
                rx_frame.id, rx_frame.dlc, rx_frame.data[0], rx_frame.data[1]);
        }
        else
        {
            printf("Rx task: queue receive error %d\n", error);
        }
    }
}

void control_task_handler(void)
{
    CAN_frame_t tx_frame = {
        .id = 0x100,
        .dlc = 4,
        .id_type = STANDARD_ID,
        .frame_type = DATA_FRAME,
        .data = {0xAA, 0xAA, 0xAA, 0xAA, 0, 0, 0, 0}
    };

    while (1)
    {
        // waits forever so the acquisition delay caused by the inversion is never cut by a timeout
        os_err_t lock_result = os_mutex_lock(&CAN_tx_mutex, OS_WAIT_FOREVER);
        if (lock_result != OS_OK)
        {
            printf("Control task: mutex lock error %d\n", lock_result);
            error_handler();
        }

        CAN_transmit(&CAN_Handle, &tx_frame);

        if (os_mutex_unlock(&CAN_tx_mutex) != OS_OK)
        {
            error_handler();
        }

        // printed outside the critical section so stdout latency is not counted as hold time
        printf("Control task: sent 0x100, released CAN TX mutex\n");

        os_task_delay(200);
    }
}

void telemetry_task_handler(void)
{
    CAN_frame_t tx_frame = {
        .id = 0x200,
        .dlc = 2,
        .id_type = STANDARD_ID,
        .frame_type = DATA_FRAME,
        .data = {0xBB, 0xBB, 0, 0, 0, 0, 0, 0}
    };

    while (1)
    {
        os_err_t lock_result = os_mutex_lock(&CAN_tx_mutex, OS_WAIT_FOREVER);
        if (lock_result != OS_OK)
        {
            printf("Telemetry task: mutex lock error %d\n", lock_result);
            error_handler();
        }

        // holds the mutex across some simulated work, this is the window interfering_task
        // preempts into and the reason control_task ends up blocked
        busy_spin(TELEMETRY_CRITICAL_SECTION_SPIN);

        CAN_transmit(&CAN_Handle, &tx_frame);

        if (os_mutex_unlock(&CAN_tx_mutex) != OS_OK)
        {
            error_handler();
        }

        printf("Telemetry task: sent 0x200, released CAN TX mutex\n");

        os_task_delay(50);
    }
}
// without priority inheritance this task preempts telemetry_task 
// while the TX mutex is held and stalls control_task
void interfering_task_handler(void)
{
    while (1)
    {
        busy_spin(INTERFERING_WORKLOAD_SPIN);

        os_task_delay(1); // yield briefly so telemetry_task can still make progress
    }
}

void CAN1_RX0_IRQHandler(void)
{
    CAN_RX_IRQHandler(&CAN_Handle, 0);
}

void CAN_rx_callback(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t fifo_num)
{
    // push the received frame to the queue
    os_queue_send_from_isr(&CAN_rx_queue, frame);
}

void busy_spin(uint32_t iterations)
{
    for (volatile uint32_t k = 0; k < iterations; k++)
    {
    }
}

void error_handler(void)
{
    while (1)
    {
        // should never get here
    }
}
