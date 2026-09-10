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

/*
@brief
	Configures the given CAN peripheral: enters init mode, exits sleep mode, configures
	bit timing and mode, then leaves init mode

@param can_handle Address of the CAN Handle structure

@retval KHAL_OK - peripheral initialized
@retval KHAL_ERR_NULL_PTR - can_handle or can_handle->CANx is NULL
@retval KHAL_ERR_INVALID_PARAM - a config field is out of range, or mode is invalid
@retval KHAL_ERR_TIMEOUT - entering/exiting init mode or exiting sleep mode timed out
*/
KHAL_status_t CAN_init(CAN_handle_t *can_handle);

/*
@brief
	Configures the specified CAN filter bank

@param filter_config Address of the filter configuration structure

@retval KHAL_OK - filter bank configured
@retval KHAL_ERR_NULL_PTR - filter_config is NULL
@retval KHAL_ERR_INVALID_PARAM - bank_num, filter_mode, filter_scale, or fifo_assignment is out of range

@note filter registers are only implemented on CAN1; this always configures through CAN1
	regardless of which CAN peripheral the filter is meant to serve
*/
KHAL_status_t CAN_configure_filter(CAN_filter_config_t *filter_config);

/*
@brief
	Enables or disables the given IRQ number in the NVIC

@param IRQ_num The IRQ number to enable/disable
@param enable ENABLE or DISABLE

@retval KHAL_OK - IRQ enabled/disabled
@retval KHAL_ERR_INVALID_PARAM - IRQ_num is greater than the highest IRQ number
*/
KHAL_status_t CAN_IRQ_control(uint8_t IRQ_num, uint8_t enable);

/*
@brief
	Sets the priority for the given IRQ number

@param IRQ_num IRQ number to set priority for
@param priority Priority value to set the IRQ to

@retval KHAL_OK - priority set
@retval KHAL_ERR_INVALID_PARAM - IRQ_num is greater than the highest IRQ number
*/
KHAL_status_t CAN_IRQ_priority_config(uint8_t IRQ_num, uint8_t priority);

/*
@brief
	Loads a frame into a free mailbox and blocks until transmission completes or times out

@param can_handle Address of the CAN Handle structure
@param frame Address of the frame to transmit

@retval KHAL_OK - frame transmitted successfully
@retval KHAL_ERR_NULL_PTR - can_handle, can_handle->CANx, or frame is NULL
@retval KHAL_ERR_INVALID_PARAM - a frame field is out of range
@retval KHAL_ERR_BUSY - no mailbox is free
@retval KHAL_ERR_TIMEOUT - the loaded mailbox never emptied within the timeout
@retval KHAL_ERR_TX - hardware reported the transmission failed
*/
KHAL_status_t CAN_transmit(CAN_handle_t *can_handle, CAN_frame_t *frame);

/*
@brief
	Checks both RX FIFOs and reads a pending message if one is available

@param can_handle Address of the CAN Handle structure
@param frame Address to store the received frame

@retval KHAL_OK - a message was read into frame
@retval KHAL_ERR_NULL_PTR - can_handle, can_handle->CANx, or frame is NULL
@retval KHAL_ERR_RX - no message pending in either FIFO

@note This is non-blocking. It just checks and returns, since a CAN transmission from another
	node can arrive at any time (or never), unlike I2C or SPI's master-initiated transfers.
	FIFO0 is checked before FIFO1
*/
KHAL_status_t CAN_receive(CAN_handle_t *can_handle, CAN_frame_t *frame);

/*
@brief
	Loads a frame into a free mailbox and returns immediately, actual transmission
	completion is handled by the CAN TX interrupt

@param can_handle Address of the CAN Handle structure
@param frame Address of the frame to transmit

@retval KHAL_OK - frame loaded and transmission requested
@retval KHAL_ERR_NULL_PTR - can_handle, can_handle->CANx, or frame is NULL
@retval KHAL_ERR_INVALID_PARAM - a frame field is out of range
@retval KHAL_ERR_BUSY - no mailbox is free
*/
KHAL_status_t CAN_transmit_IT(CAN_handle_t *can_handle, CAN_frame_t *frame);

/*
@brief
	Decodes a received frame from the given FIFO and invokes CAN_rx_callback

@param can_handle Address of the CAN Handle structure
@param fifo_num Which FIFO triggered the interrupt

@note call this from the app's CAN1_RX0_IRQHandler/CAN1_RX1_IRQHandler with the matching fifo_num
*/
void CAN_IRQHandler(CAN_handle_t *can_handle, uint8_t fifo_num);

/*
@brief
	Weak default callback invoked by CAN_IRQHandler() after a received frame has been
	decoded. Override this in app code to route the frame (e.g. push to a queue)

@param can_handle Address of the CAN Handle structure
@param frame Address of the decoded frame
@param fifo_num Which FIFO the frame was received from
*/
__attribute__((weak)) void CAN_rx_callback(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t fifo_num);

#endif
