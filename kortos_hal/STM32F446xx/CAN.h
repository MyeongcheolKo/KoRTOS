#ifndef KORTOS_HAL_STM32F446XX_CAN_H_
#define KORTOS_HAL_STM32F446XX_CAN_H_

#include "STM32F446xx.h"
#include "KHAL_common.h"

typedef enum
{
    CAN_MODE_NORMAL,
    CAN_MODE_LOOPBACK,
    CAN_MODE_SILENT,
    CAN_MODE_SILENT_LOOPBACK
} CAN_mode_t;

typedef struct
{
    uint32_t baud_rate_prescaler; // baud rate prescaler value, 0-1023 (actual prescaler = BRP + 1)
    uint8_t  ts1; // number of time quanta for time segment 1, 0-15 (actual segment = value + 1 tq)
    uint8_t  ts2; // number of time quanta for time segment 2, 0-7 (actual segment = value + 1 tq)
    uint8_t  sjw; //resynchronization jump width, 0-3  (actual width = value + 1 tq)
    CAN_mode_t  mode; // Normal/Loopback/Silent/Silent+Loopback
    uint8_t ttcm; // time triggered communication mode, ENABLE or DISABLE
} CAN_config_t;

typedef struct
{
    CAN_reg_t *CANx;
    CAN_config_t can_config;
} CAN_handle_t;

typedef enum 
{
    FILTER_MODE_MASK,
    FILTER_MODE_LIST
} CAN_filter_mode_t;

typedef enum 
{
    FILTER_SCALE_16BIT,
    FILTER_SCALE_32BIT
} CAN_filter_scale_t;

typedef enum 
{
    FIFO0,
    FIFO1
} CAN_fifo_assignment_t;

typedef struct
{
    uint8_t bank_num; // filter bank number, 0-27
    CAN_filter_mode_t filter_mode; // filter mode, mask or list
    CAN_filter_scale_t filter_scale; // filter scale, 16-bit or 32-bit
    CAN_fifo_assignment_t fifo_assignment; // FIFO assignment, FIFO0 or FIFO1
    uint32_t fr1; // filter bank register 1 value
    uint32_t fr2; // filter bank register 2 value
} CAN_filter_config_t;

typedef enum
{
    STANDARD_ID,
    EXTENDED_ID
} CAN_id_type_t;

typedef enum
{
    DATA_FRAME,
    REMOTE_FRAME
} CAN_frame_type_t;

typedef struct 
{
    uint32_t id; // CAN identifier, 11-bit for standard ID, 29-bit for extended ID
    uint8_t dlc; // data length code, 0-8
    CAN_id_type_t id_type; // standard or extended ID
    CAN_frame_type_t frame_type; // data frame or remote frame
    uint8_t data[8]; // data bytes
    uint32_t timestamp; // timestamp of the received message
} CAN_frame_t;


KHAL_status_t CAN_init(CAN_handle_t *can_handle);

KHAL_status_t CAN_configure_filter(CAN_filter_config_t *filter_config);

KHAL_status_t CAN_IRQ_control(uint8_t IRQ_num, uint8_t enable);

KHAL_status_t CAN_IRQ_priority_config(uint8_t IRQ_num, uint8_t priority);

KHAL_status_t CAN_transmit(CAN_handle_t *can_handle, CAN_frame_t *frame);

KHAL_status_t CAN_receive(CAN_handle_t *can_handle, CAN_frame_t *frame);

KHAL_status_t CAN_transmit_IT(CAN_handle_t *can_handle, CAN_frame_t *frame);

void CAN_IRQHandler(CAN_handle_t *can_handle, uint8_t fifo_num);

__attribute__((weak)) void CAN_rx_callback(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t fifo_num);

#endif
