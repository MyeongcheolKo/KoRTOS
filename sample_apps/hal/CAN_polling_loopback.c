#include <stdint.h>
#include <stdio.h>
#include "GPIO.h"
#include "CAN.h"

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
    CAN_handle_t CAN_Handle;
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

    // transmit a message, use mailbox 0
    CAN_frame_t tx_frame;
    tx_frame.id_type = STANDARD_ID;
    tx_frame.frame_type = DATA_FRAME;
    tx_frame.id = 0x123;
    tx_frame.dlc = 2;
    tx_frame.data[0] = 0xAB;
    tx_frame.data[1] = 0xCD;
    if (CAN_transmit(&CAN_Handle, &tx_frame) != KHAL_OK)
    {
        printf("CAN1 failed to transmit message\n");
        while (1);
    }

    // receive a message
    CAN_frame_t rx_frame;
    if (CAN_receive(&CAN_Handle, &rx_frame) != KHAL_OK)
    {
        printf("CAN1 failed to receive message\n");
        while (1);
    }
    // Expected output: "Received message: ID=0x123, DLC=2, Data=0xAB 0xCD"
    printf("Received message: ID=0x%03lX, DLC=%d, Data=0x%02X 0x%02X\n", rx_frame.id, rx_frame.dlc, rx_frame.data[0], rx_frame.data[1]);

    return 0;
}