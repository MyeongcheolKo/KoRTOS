#include "CAN.h"

#define CAN_INIT_TIMEOUT 1000000

#define MAX_CAN_FILTER_BANKS 27

#define CAN_BTR_BRP_MAX (CAN_BTR_BRP_MSK >> CAN_BTR_BRP_POS)
#define CAN_BTR_TS1_MAX (CAN_BTR_TS1_MSK >> CAN_BTR_TS1_POS)
#define CAN_BTR_TS2_MAX (CAN_BTR_TS2_MSK >> CAN_BTR_TS2_POS)
#define CAN_BTR_SJW_MAX (CAN_BTR_SJW_MSK >> CAN_BTR_SJW_POS)

#define CAN_MAX_IRQ_NUM 96 // highest implemented IRQ number on the STM32F446xx

// private function prototypes
void CAN_clock_control(CAN_reg_t *p_CANx, uint8_t enable);
KHAL_status_t CAN_load_mailbox(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t *out_mailbox);
KHAL_status_t CAN_read_rx_frame(CAN_reg_t *CANx, uint8_t fifo_num, CAN_frame_t *frame);

__attribute__((weak)) void CAN_rx_callback(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t fifo_num) { /* default: no-op */ }

KHAL_status_t CAN_init(CAN_handle_t *can_handle)
{
    // check for null pointer
    if (can_handle == NULL || can_handle->CANx == NULL) return KHAL_ERR_NULL_PTR;
    
    // check if the configuration parameters are valid
    if (can_handle->can_config.baud_rate_prescaler > CAN_BTR_BRP_MAX
        || can_handle->can_config.ts1 > CAN_BTR_TS1_MAX
        || can_handle->can_config.ts2 > CAN_BTR_TS2_MAX
        || can_handle->can_config.sjw > CAN_BTR_SJW_MAX
        || can_handle->can_config.mode > CAN_MODE_SILENT_LOOPBACK
        || (can_handle->can_config.ttcm != ENABLE && can_handle->can_config.ttcm != DISABLE))
    {
        return KHAL_ERR_INVALID_PARAM;
    }

    CAN_reg_t *CANx = can_handle->CANx;

    // enable the clock for the CAN peripheral
    CAN_clock_control(CANx, ENABLE);

    // request init mode, set INRQ bit in MCR
    CANx->MCR |= CAN_MCR_INRQ_MSK;

    // wait for init mode ACK, when it is set, it entered init mode
    uint32_t timeout = CAN_INIT_TIMEOUT;
    while (!(CANx->MSR & CAN_MSR_INAK_MSK) && timeout > 0)
    {
        timeout--;
    }
    if (timeout == 0)
    {
        return KHAL_ERR_TIMEOUT;
    }

    // exit sleep mode, clear sleep bit in MCR
    CANx->MCR &= ~CAN_MCR_SLEEP_MSK;

    // wait for sleep mode ACK, when it is cleared, it exited sleep mode
    timeout = CAN_INIT_TIMEOUT;
    while ((CANx->MSR & CAN_MSR_SLAK_MSK) && timeout > 0) 
    {
        timeout--;
    }
    if (timeout == 0)
    {
        return KHAL_ERR_TIMEOUT;
    }

    // determine loopback and silent mode based on config
    uint8_t lookpack_mode = 0;
    uint8_t silent_mode = 0;
    switch (can_handle->can_config.mode)
    {
        case CAN_MODE_NORMAL:
            lookpack_mode = 0;
            silent_mode = 0;
            break;
        case CAN_MODE_LOOPBACK:
            lookpack_mode = 1;
            silent_mode = 0;
            break;
        case CAN_MODE_SILENT:
            lookpack_mode = 0;
            silent_mode = 1;
            break;
        case CAN_MODE_SILENT_LOOPBACK:
            lookpack_mode = 1;
            silent_mode = 1;
            break;
        default:
            return KHAL_ERR_INVALID_PARAM;
    }

    // configure bit timing
    uint32_t btr = CANx->BTR;
    btr &= ~(CAN_BTR_BRP_MSK 
            | CAN_BTR_TS1_MSK 
            | CAN_BTR_TS2_MSK 
            | CAN_BTR_SJW_MSK 
            | CAN_BTR_LBKM_MSK 
            | CAN_BTR_SILM_MSK); // clear all the bits going to be set
    btr |= (can_handle->can_config.baud_rate_prescaler << CAN_BTR_BRP_POS)
            | (can_handle->can_config.ts1 << CAN_BTR_TS1_POS)
            | (can_handle->can_config.ts2 << CAN_BTR_TS2_POS)
            | (can_handle->can_config.sjw << CAN_BTR_SJW_POS)
            | (lookpack_mode << CAN_BTR_LBKM_POS)
            | (silent_mode << CAN_BTR_SILM_POS);
    CANx->BTR = btr;

    // configure time triggered communication mode
    if (can_handle->can_config.ttcm == ENABLE)
    {
        CANx->MCR |= CAN_MCR_TTCM_MSK;
    }
    else
    {
        CANx->MCR &= ~CAN_MCR_TTCM_MSK;
    }
    
    // leave init mode
    CANx->MCR &= ~CAN_MCR_INRQ_MSK;

    // wait for init mode ACK to clear, when it is cleared, it exited init mode
    timeout = CAN_INIT_TIMEOUT;
    while ((CANx->MSR & CAN_MSR_INAK_MSK) && timeout > 0)
    {
        timeout--;
    }
    if (timeout == 0)
    {
        return KHAL_ERR_TIMEOUT;
    }

    return KHAL_OK;
}

KHAL_status_t CAN_configure_filter(CAN_filter_config_t *filter_config)
{
    // CAN1 is hardcoded because the STM32F446xx only implements the filter banks once
    // and its wired to CAN1. CAN2 uses the same filter banks as CAN1.

    // check for null pointer
    if (filter_config == NULL) return KHAL_ERR_NULL_PTR;

    // check if the config is valid
    if (filter_config->bank_num > MAX_CAN_FILTER_BANKS 
        || filter_config->filter_mode > FILTER_MODE_LIST 
        || filter_config->filter_scale > FILTER_SCALE_32BIT 
        || filter_config->fifo_assignment > FIFO1)
    {
        return KHAL_ERR_INVALID_PARAM;
    }

    // enter filter init mode
    CAN1->FMR |= CAN_FMR_FINIT_MSK; 

    // deactivate specified filter bank 
    uint8_t bank_num = filter_config->bank_num;
    CAN1->FA1R &= ~(1 << bank_num);

    // set filter bank to specified mode
    if (filter_config->filter_mode == FILTER_MODE_MASK)
    {
        CAN1->FM1R &= ~(1 << bank_num);
    }
    else // FILTER_MODE_LIST
    {
        CAN1->FM1R |= (1 << bank_num);
    }

    // set filter bank to specified scale
    if (filter_config->filter_scale == FILTER_SCALE_16BIT)
    {
        CAN1->FS1R &= ~(1 << bank_num);
    }
    else  // FILTER_SCALE_32BIT
    {
        CAN1->FS1R |= (1 << bank_num);
    }

    // set filter bank to specified FIFO assignment
    if (filter_config->fifo_assignment == FIFO0)
    {
        CAN1->FFA1R &= ~(1 << bank_num);
    }
    else // FIFO1
    {
        CAN1->FFA1R |= (1 << bank_num);
    }

    // set filter bank register values
    CAN1->filter_banks[bank_num].FR1 = filter_config->fr1;
    CAN1->filter_banks[bank_num].FR2 = filter_config->fr2;

    // activate specified filter bank
    CAN1->FA1R |= (1 << bank_num);

    // exit filter init mode
    CAN1->FMR &= ~CAN_FMR_FINIT_MSK; 

    return KHAL_OK;
}

KHAL_status_t CAN_IRQ_control(uint8_t IRQ_num, uint8_t enable)
{
    // check if the irq_num is valid
    if (IRQ_num > CAN_MAX_IRQ_NUM) return KHAL_ERR_INVALID_PARAM;

    // enable or disable the specified IRQ number in the NVIC
	if(enable == ENABLE)
	{
        NVIC->ISER[IRQ_num / 32] |= (1 << (IRQ_num % 32));
	}
    else // disable the IRQ
    {
        NVIC->ICER[IRQ_num / 32] |= (1 << (IRQ_num % 32));
    }

    return KHAL_OK;
}

KHAL_status_t CAN_IRQ_priority_config(uint8_t IRQ_num, uint8_t priority)
{
    // check if the irq_num is valid
    if (IRQ_num > CAN_MAX_IRQ_NUM) return KHAL_ERR_INVALID_PARAM;

    // set the priority for the specified IRQ number
    NVIC->IPR[IRQ_num] = (priority << 4); // shift left by 4 because the lower 4 bits are unimplemented in the STM32F446xx

    return KHAL_OK;
}

KHAL_status_t CAN_transmit(CAN_handle_t *can_handle, CAN_frame_t *frame)
{
    // load the mailbox
    uint8_t mailbox;
    KHAL_status_t status = CAN_load_mailbox(can_handle, frame, &mailbox);
    if (status != KHAL_OK)
    {
        return status;
    }
    
    CAN_reg_t *CANx = can_handle->CANx;

    // wait for mailbox 0 to be empty, which means the message has been transmitted
    uint32_t timeout = CAN_INIT_TIMEOUT;
    switch (mailbox)
    {
        case 0:
            while (!(CANx->TSR & CAN_TSR_TME0_MSK) && timeout > 0)
            {
                timeout--;
            }
            if (timeout == 0)
            {
                return KHAL_ERR_TIMEOUT;
            }
            // check if the tranmission was successful
            if (!(CANx->TSR & CAN_TSR_TXOK0_MSK))
            {
                return KHAL_ERR_TX;
            }
            break;
        case 1:
            while (!(CANx->TSR & CAN_TSR_TME1_MSK) && timeout > 0)
            {
                timeout--;
            }
            if (timeout == 0)
            {
                return KHAL_ERR_TIMEOUT;
            }
            // check if the tranmission was successful
            if (!(CANx->TSR & CAN_TSR_TXOK1_MSK))
            {
                return KHAL_ERR_TX;
            }
            break;
        case 2:
            while (!(CANx->TSR & CAN_TSR_TME2_MSK) && timeout > 0)
            {
                timeout--;
            }
            if (timeout == 0)
            {
                return KHAL_ERR_TIMEOUT;
            }
            // check if the tranmission was successful
            if (!(CANx->TSR & CAN_TSR_TXOK2_MSK))
            {
                return KHAL_ERR_TX;
            }
            break;
        default:
            return KHAL_ERR_TX; 
    }

    // successfully transmitted the message
    return KHAL_OK;
}

KHAL_status_t CAN_receive(CAN_handle_t *can_handle, CAN_frame_t *frame)
{
    // check for null pointer
    if (can_handle == NULL || can_handle->CANx == NULL || frame == NULL) return KHAL_ERR_NULL_PTR;

    CAN_reg_t *CANx = can_handle->CANx;

    // check which FIFO has a message, FIFO 0 has higher priority than FIFO 1
    uint8_t fifo_num = 0xFF; // invalid FIFO number
    if (CANx->RF0R & CAN_RFxR_FMPx_MSK)
    {
        // FIFO 0 has a message
        fifo_num = 0;
    }
    else if (CANx->RF1R & CAN_RFxR_FMPx_MSK)
    {
        // FIFO 1 has a message
        fifo_num = 1;
    }
    else
    {
        // no message in either FIFO
        return KHAL_ERR_RX;
    }

    // read the message from the FIFO
    return CAN_read_rx_frame(CANx, fifo_num, frame);
}

KHAL_status_t CAN_transmit_IT(CAN_handle_t *can_handle, CAN_frame_t *frame)
{
    uint8_t mailbox;
    // return immediately after loading the mailbox, IRQ will handle the rest
    return CAN_load_mailbox(can_handle, frame, &mailbox); 
}

void CAN_IRQHandler(CAN_handle_t *can_handle, uint8_t fifo_num)
{
    // check for null pointer
    if (can_handle == NULL || can_handle->CANx == NULL) return;

    CAN_frame_t frame;
    // read the message from the FIFO
    CAN_read_rx_frame(can_handle->CANx, fifo_num, &frame);

    // call the user defined callback function
    CAN_rx_callback(can_handle, &frame, fifo_num);
}

/*-----private helper functions-----*/

void CAN_clock_control(CAN_reg_t *p_CANx, uint8_t enable)
{
    if(enable == ENABLE)
    {
        if(p_CANx == CAN1)
        {
            CAN1_PCLK_EN();
        }
        else if(p_CANx == CAN2)
        {
            CAN2_PCLK_EN();
        }
    }
    else
    {
        if(p_CANx == CAN1)
        {
            CAN1_PCLK_DI();
        }
        else if(p_CANx == CAN2)
        {
            CAN2_PCLK_DI();
        }
    }
}

KHAL_status_t CAN_load_mailbox(CAN_handle_t *can_handle, CAN_frame_t *frame, uint8_t *out_mailbox)
{
    // check for null pointer
    if (can_handle == NULL 
        || can_handle->CANx == NULL 
        || frame == NULL 
        || out_mailbox == NULL) return KHAL_ERR_NULL_PTR;

    // check if the frame is valid
    if (frame->dlc > 8 
        || frame->id_type > EXTENDED_ID 
        || frame->frame_type > REMOTE_FRAME
        || (frame->id_type == STANDARD_ID && frame->id > 0x7FF)
        || (frame->id_type == EXTENDED_ID && frame->id > 0x1FFFFFFF))
    {
        return KHAL_ERR_INVALID_PARAM;
    }

    CAN_reg_t *CANx = can_handle->CANx;

    // check if there is a free mailbox
    if (!(CANx->TSR & (CAN_TSR_TME0_MSK | CAN_TSR_TME1_MSK | CAN_TSR_TME2_MSK)))
    {
        return KHAL_ERR_BUSY;
    }

    // get the free mailbox 
    uint8_t mailbox = (CANx->TSR & CAN_TSR_CODE_MSK) >> CAN_TSR_CODE_POS;

    // write the id and frame type
    uint32_t tir_value = (frame->id_type << CAN_TIxR_IDE_POS)
                        | (frame->frame_type << CAN_TIxR_RTR_POS);

    if (frame->id_type == STANDARD_ID)
    {
        tir_value |= (frame->id << CAN_TIxR_STID_POS);
    }
    else // EXTENDED_ID
    {
        tir_value |= (frame->id << CAN_TIxR_EXID_POS);
    }
    CANx->tx_mailboxes[mailbox].TIR = tir_value; 

    // write the data length 
    CANx->tx_mailboxes[mailbox].TDTR = (frame->dlc << CAN_TDTxR_DLC_POS);

    // write the data bytes
    CANx->tx_mailboxes[mailbox].TDLR = frame->data[3] << 24 
                                        | frame->data[2] << 16 
                                        | frame->data[1] << 8 
                                        | frame->data[0];
    CANx->tx_mailboxes[mailbox].TDHR = frame->data[7] << 24 
                                        | frame->data[6] << 16 
                                        | frame->data[5] << 8 
                                        | frame->data[4];

    // request transmission
    CANx->tx_mailboxes[mailbox].TIR |= CAN_TIxR_TXRQ_MSK;

    // return the mailbox number
    *out_mailbox = mailbox;
    return KHAL_OK;
}

KHAL_status_t CAN_read_rx_frame(CAN_reg_t *CANx, uint8_t fifo_num, CAN_frame_t *frame)
{
    // check for null pointer and valid FIFO number
    if (frame == NULL || CANx == NULL) return KHAL_ERR_NULL_PTR;
    if (fifo_num > 1) return KHAL_ERR_INVALID_PARAM;

    // read the message from the FIFO
    uint8_t id_type = (CANx->rx_fifos[fifo_num].RIxR & CAN_RIxR_IDE_MSK) >> CAN_RIxR_IDE_POS;
    if (id_type == STANDARD_ID)
    {
        frame->id = (CANx->rx_fifos[fifo_num].RIxR & CAN_RIxR_STID_MSK) >> CAN_RIxR_STID_POS;
    }
    else // EXTENDED_ID, the IDE is one bit so can only be 0 or 1
    {
        frame->id = (CANx->rx_fifos[fifo_num].RIxR & CAN_RIxR_EXID_MSK) >> CAN_RIxR_EXID_POS;
    }
    frame->dlc = (CANx->rx_fifos[fifo_num].RDTxR & CAN_RDTxR_DLC_MSK) >> CAN_RDTxR_DLC_POS;
    frame->data[0] = (CANx->rx_fifos[fifo_num].RDLxR & 0xFF);
    frame->data[1] = (CANx->rx_fifos[fifo_num].RDLxR >> 8) & 0xFF;
    frame->data[2] = (CANx->rx_fifos[fifo_num].RDLxR >> 16) & 0xFF;
    frame->data[3] = (CANx->rx_fifos[fifo_num].RDLxR >> 24) & 0xFF;
    frame->data[4] = (CANx->rx_fifos[fifo_num].RDHxR & 0xFF);
    frame->data[5] = (CANx->rx_fifos[fifo_num].RDHxR >> 8) & 0xFF;
    frame->data[6] = (CANx->rx_fifos[fifo_num].RDHxR >> 16) & 0xFF;
    frame->data[7] = (CANx->rx_fifos[fifo_num].RDHxR >> 24) & 0xFF;
    frame->timestamp = (CANx->rx_fifos[fifo_num].RDTxR & CAN_RDTxR_TIME_MSK) >> CAN_RDTxR_TIME_POS;

    // release the message from FIFO 0
    if (fifo_num == 0) // FIFO0
    {
        CANx->RF0R |= CAN_RFxR_RFOMx_MSK;
    }
    else 
    {
        CANx->RF1R |= CAN_RFxR_RFOMx_MSK;
    }

    return KHAL_OK;
}