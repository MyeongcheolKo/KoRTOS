/*
 * USART.c
 *
 *  Created on: Jan 3, 2026
 *      Author: krisko
 */

#include "USART.h"

/**
 * @brief		This function enable/disable the clock for the given USART peripheral
 *
 * @param		p_USARTx address of USART peripheral
 * @param		enable ENABLE or DISABLE
 */
void USART_clock_control(USART_reg_t *p_USARTx, uint8_t enable)
{
	if(enable == ENABLE)
	{
		if(p_USARTx == USART1)
		{
			USART1_PCLK_EN();
		}
		else if(p_USARTx == USART2)
		{
			USART2_PCLK_EN();
		}
		else if(p_USARTx == USART3)
		{
			USART3_PCLK_EN();
		}
		else if(p_USARTx == UART4)
		{
			UART4_PCLK_EN();
		}
		else if(p_USARTx == UART5)
		{
			UART5_PCLK_EN();
		}
		else if(p_USARTx == USART6)
		{
			USART6_PCLK_EN();
		}
	}else
	{
		if(p_USARTx == USART1)
		{
			USART1_PCLK_DI();
		}
		else if(p_USARTx == USART2)
		{
			USART2_PCLK_DI();
		}
		else if(p_USARTx == USART3)
		{
			USART3_PCLK_DI();
		}
		else if(p_USARTx == UART4)
		{
			UART4_PCLK_DI();
		}
		else if(p_USARTx == UART5)
		{
			UART5_PCLK_DI();
		}
		else if(p_USARTx == USART6)
		{
			USART6_PCLK_DI();
		}
	}
}

/**
 * @brief		This function configures the given USART peripheral
 *
 * @param		p_USART_Handle address of the Handle structure of the USART peripheral
 */
void USART_init(USART_Handle_t *p_USART_Handle)
{
	uint32_t temp1;
	uint8_t temp2;

	/*
	 * configure CR1
	 */
	temp1 = p_USART_Handle->p_USARTx->CR1;
	//configure mode
	temp2 = p_USART_Handle->USARTx_config.USART_mode;
	if (temp2 == USART_MODE_ONLY_RX)
	{
		temp1 |= (1 << USART_CR1_RE);
	}
	else if(temp2 == USART_MODE_ONLY_TX)
	{
		temp1 |= (1 << USART_CR1_TE);
	}
	else if (temp2 == USART_MODE_TXRX)
	{

		temp1 |= ( (1 << USART_CR1_RE) | (1 << USART_CR1_TE) );
	}

	//configure word length
	temp2 = p_USART_Handle->USARTx_config.USART_word_len;
	if (temp2 == USART_WORD_LEN_8BITS)
	{
		temp1 &= ~(1 << USART_CR1_M);
	}
	else if (temp2 == USART_WORD_LEN_9BITS)
	{
		temp1 |= (1 << USART_CR1_M);
	}

	//configure parity
	temp2 = p_USART_Handle->USARTx_config.USART_parity;
	if (temp2 == USART_PARITY_DISABLE)
	{
		temp1 &= ~(1 << USART_CR1_PCE);
	}
	else
	{
		temp1 |= (1 << USART_CR1_PCE);
		if (temp2 == USART_PARITY_EN_EVEN)
		{
			temp1 &= ~(1 << USART_CR1_PS);
		}
		else if (temp2 == USART_PARITY_EN_ODD)
		{
			temp1 |= (1 << USART_CR1_PS);
		}
	}

	p_USART_Handle->p_USARTx->CR1 = temp1;

	/*
	 * configure CR2
	 */
	temp1 = p_USART_Handle->p_USARTx->CR2;

	//configure number of stop bits
	temp2 = p_USART_Handle->USARTx_config.USART_num_stop_bits;

	//clear stop bits first
	temp1 &= ~(0x3 << USART_CR2_STOP);
	if(temp2 == USART_STOP_BITS_1)
	{
		temp1 &= ~(1 << USART_CR2_STOP);
	}
	else if (temp2 == USART_STOP_BITS_0_5)
	{
		temp1 |= (1 << USART_CR2_STOP);
	}
	else if (temp2 == USART_STOP_BITS_1_5)
	{
		temp1 |= (2 << USART_CR2_STOP);
	}
	else if (temp2 == USART_STOP_BITS_2)
	{
		temp1 |= (3 << USART_CR2_STOP);
	}

	p_USART_Handle->p_USARTx->CR2 = temp1;

	/*
	 * configure CR3
	 */
	temp1 = p_USART_Handle->p_USARTx->CR3;
	//configure hardware flow control(CTS and RTS)
	temp2 = p_USART_Handle->USARTx_config.USART_HW_flow_ctrl;
	if(temp2 == USART_HW_FLOW_CTRL_CTS)
	{
		temp1 |= (1 << USART_CR3_CTSE);
	}
	else if (temp2 == USART_HW_FLOW_CTRL_RTS)
	{
		temp1 |= (1 << USART_CR3_RTSE);
	}
	else if (temp2 == USART_HW_FLOW_CTRL_CTS_RTS)
	{
		temp1 |= ((1 << USART_CR3_RTSE) | (1 << USART_CR3_CTSE));
	}
	p_USART_Handle->p_USARTx->CR3 = temp1;

	/*
	 * configure BRR(baud rate)
	 */
	uint32_t brr_val = 0;

	uint32_t pclkx_val, usartdiv, M_part,F_part;
	if(p_USART_Handle->p_USARTx == USART1 || p_USART_Handle->p_USARTx == USART6)
	{
		//USART1 and USART6 are on APB2 bus
		pclkx_val = RCC_get_pclk2();
	}
	else
	{
		//USART2, USART3, UART4, UART5 are on APB1 bus
		pclkx_val = RCC_get_pclk1();
	}

	//check for OVER8 configuration
	uint8_t over8_config = ( (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_OVER8) & 1 );
	if(over8_config)
	{
		//over sampling by 8
		usartdiv = 100 * (pclkx_val) / (8 * p_USART_Handle->USARTx_config.USART_baud);
	}
	else
	{
		//over sampling by 16
		usartdiv = 100 * (pclkx_val) / (16 * p_USART_Handle->USARTx_config.USART_baud);
	}

	//configure matissa part of BRR
	M_part = usartdiv / 100;
	brr_val |= M_part << 4;

	//calculate fraction part of BRR
	F_part = usartdiv % 100;
	if(over8_config)
	{
		//over sampling by 8
		F_part = ( ((F_part * 8) + 50) / 100 ) & ((uint8_t)0x07);	//0x07 b/c for OVER8=1, the [3] bit is not considered and must be kept cleared
	}
	else
	{
		//over sampling by 16
		F_part = ( ((F_part * 16) + 50) / 100 ) & ((uint8_t)0x0F);
	}
	brr_val |= F_part;

	p_USART_Handle->p_USARTx->BRR = brr_val;


}

/**
 * @brief		This function disables the clock of the given USART peripheral
 *
 * @param		p_USARTx address of the USART peripheral
 */
void USART_deinit(USART_reg_t *p_USARTx){
	if(p_USARTx == USART1)
	{
		USART1_REG_RESET();
	}
	else if(p_USARTx == USART2)
	{
		USART2_REG_RESET();
	}
	else if(p_USARTx == USART3)
	{
		USART3_REG_RESET();
	}
	else if(p_USARTx == UART4)
	{
		UART4_REG_RESET();
	}
	else if(p_USARTx == UART5)
	{
		UART5_REG_RESET();
	}
	else if(p_USARTx == USART6)
	{
		USART6_REG_RESET();
	}
}

/**
 * @brief		This function sends the given length of data in the given Tx buffer to the USART peripheral
 *
 * @param		p_USART_Handle the USART peripheral to send data to
 * @param		p_Tx_buffer address of Tx buffer storing the data to send
 * @param		len length of data to send
 */
void USART_send(USART_Handle_t *p_USART_Handle, uint8_t *p_Tx_buffer, uint32_t len)
{
	uint16_t *p_data;
	for(uint32_t i = 0; i < len; i++)
	{
		while(USART_get_flag_status(p_USART_Handle->p_USARTx, USART_SR_TXE) == 0);

		if(p_USART_Handle->USARTx_config.USART_word_len == USART_WORD_LEN_9BITS)
		{
			//word len is 9 bits
			if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
			{
				//parity is disabled so 9 bits of data will be sent
				p_data = (uint16_t*)p_Tx_buffer;	//get 2 bytes of data
				p_USART_Handle->p_USARTx->DR = (*p_data & (uint16_t)0x01ff); //only need the 9 bits out of the 2 bytes
				p_Tx_buffer += 2;	//increment by 2 (2 bytes)
			}
			else
			{
				//parity is enabled to either ODD or EVEN, so only 8 bits will be sent
				p_USART_Handle->p_USARTx->DR = *p_Tx_buffer;
				p_Tx_buffer++;		//increment by 1 (1 byte)
			}
		}
		else
		{
			//word length is 8 bits
			if (p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
			{
				//all 8 bits are data, no parity
				p_USART_Handle->p_USARTx->DR = *p_Tx_buffer;
			}
			else
			{
				//only 7 bits are data, parity is enabled
				p_USART_Handle->p_USARTx->DR = (*p_Tx_buffer & 0x7F);
			}
			p_Tx_buffer++;
		}
	}
	//wait for transfer to be complete
	while(USART_get_flag_status(p_USART_Handle->p_USARTx, USART_SR_TC) == 0);
}

/**
 * @brief		This function receives the given length of data to the given Rx buffer from the USART peripheral
 *
 * @param		p_USART_Handle the USART peripheral to receive data from
 * @param		p_Rx_buffer address of Rx buffer storing the data received
 * @param		len length of data to receive
 */
void USART_receive(USART_Handle_t *p_USART_Handle, uint8_t *p_Rx_buffer, uint32_t len)
{
	for(uint32_t i = 0; i < len; i++)
	{
		//wait until RXNE is set
		while(USART_get_flag_status(p_USART_Handle->p_USARTx, USART_SR_RXNE) == 0);

		if(p_USART_Handle->USARTx_config.USART_word_len == USART_WORD_LEN_9BITS)
		{
			//receive 9 bits of data
			if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
			{
				//parity is not used, so all 9 bits are data
				*(uint16_t*)p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint16_t)0x01ff);
				p_Rx_buffer += 2;
			}
			else
			{
				//parity is used, so only 8 bits are data and 1 is parity
				*p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0xff);
				p_Rx_buffer++;
			}
		}
		else
		{
			//receive 8 bits of data
			if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
			{
				//parity is not used, so all 8 bits are data
				*p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0xff);
			}
			else
			{
				//parity is used, so only 7 bits are data and 1 is parity
				*p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0x7f);
			}
			p_Rx_buffer++;
		}


	}
}

/**
 * @brief		This function starts the transmission process and enables the interrupts
 *
 * @param		p_USART_Handle the USART peripheral to send data to
 * @param		p_Tx_buffer address of Tx buffer storing the data to send
 * @param		len length of data to send
 */
uint8_t USART_send_IT(USART_Handle_t *p_USART_Handle, uint8_t *p_Tx_buffer, uint32_t len)
{
	uint8_t tx_state = p_USART_Handle->Tx_state;

	if(tx_state == USART_STATE_READY)
	{
		p_USART_Handle->p_Tx_buffer = p_Tx_buffer;
		p_USART_Handle->Tx_len = len;
		p_USART_Handle->Tx_state = USART_STATE_BUSY_TX;

		//enable TXEIE (Tx buffer empty interrupt)
		p_USART_Handle->p_USARTx->CR1 |= (1 << USART_CR1_TXEIE);

		//enable TCIE (transmission complete interrupt)
		p_USART_Handle->p_USARTx->CR1 |= (1 << USART_CR1_TCIE);
	}

	return tx_state;
}

/**
 * @brief		This function starts the reception process and enables the interrupts
 *
 * @param		p_USART_Handle the USART peripheral to receive data from
 * @param		p_Rx_buffer address of Rx buffer storing the data received
 * @param		len length of data to receive
 */
uint8_t USART_receive_IT(USART_Handle_t *p_USART_Handle, uint8_t *p_Rx_buffer, uint32_t len)
{
	uint8_t rx_state = p_USART_Handle->Rx_state;

	if(rx_state == USART_STATE_READY)
	{
		p_USART_Handle->p_Rx_buffer = p_Rx_buffer;
		p_USART_Handle->Rx_len = len;
		p_USART_Handle->Rx_state = USART_STATE_BUSY_RX;

		//enable RXNEIE (Rx buffer empty interrupt)
		p_USART_Handle->p_USARTx->CR1 |= (1 << USART_CR1_RXNEIE);
	}

	return rx_state;
}

/**
 * @brief		This function enable/disable interrupt for the given peripheral
 *
 * @param		IRQ_num the IRQ number to enable/disable
 * @param		enable ENABLE or DISABLE the IRQ
 */
void USART_IRQ_config(uint8_t IRQ_num, uint8_t enable)
{
	if(enable == ENABLE)
	{
		NVIC->ISER[IRQ_num / 32] |= (1 << (IRQ_num % 32));
	}
	else
	{
		NVIC->ICER[IRQ_num / 32] |= (1 << (IRQ_num % 32));
	}
}

/**
 * @brief		This function enable/disable the GPIO pin as given
 *
 * @param		IRQ_num IRQ number of the peripheral to set priority
 * @param		IRQ_priority priority value to set the IRQ to
 */
void USART_set_priority(uint8_t IRQ_num, uint8_t IRQ_priority)
{
	NVIC->IPR[IRQ_num] = (IRQ_priority << 4); //shift left by 4 because the lower 4 bits are unimplemented in the STM32F446xx
}


/**
 * @brief		This function identifies the interrupt source (TC, TXE, RXNE, or error flags) and
 * 				handles data transmission/reception or notifies the user application accordingly
 *
 * @param		p_USART_Handle address of the USART Handle structure
 */
void USART_IRQ_handling(USART_Handle_t *p_USART_Handle)
{
	uint32_t temp1, temp2, temp3;
	int8_t dummy_byte;
	/*
	 * check for TC flag
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_TC) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_TCIE) & 1;
	if(temp1 && temp2)
	{
		//the interrupt was caused by TC

		//close transmission and call application callback
		if( p_USART_Handle->Tx_state == USART_STATE_BUSY_TX && p_USART_Handle->Tx_len == 0)
		{
			//clear TC flag
			p_USART_Handle->p_USARTx->SR &= ~(1 << USART_SR_TC);

			//close TCIE(tranmission complete interrupt)
			p_USART_Handle->p_USARTx->CR1 &= ~(1 << USART_CR1_TCIE);

			//reset USART Handle tx variables
			p_USART_Handle->Tx_state = USART_STATE_READY;
			p_USART_Handle->p_Tx_buffer = NULL;
			p_USART_Handle->Tx_len = 0;

			//notify user application
			USART_event_callback(p_USART_Handle, USART_EV_TX_CMPLT);
		}
	}

	/*
	 * check for TXE flag
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_TXE) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_TXEIE) & 1;
	if(temp1 && temp2)
	{
		//the interrupt was caused by TXE

		if(p_USART_Handle->Tx_state == USART_STATE_BUSY_TX)
		{
			//send data if tx_len is not 0
			if(p_USART_Handle->Tx_len > 0)
			{
				if(p_USART_Handle->USARTx_config.USART_word_len == USART_WORD_LEN_9BITS)
				{
					//word len is 9 bits
					if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
					{
						//parity is disabled so 9 bits of data will be sent
						uint16_t *p_data = (uint16_t*)p_USART_Handle->p_Tx_buffer;	//get 2 bytes of data
						p_USART_Handle->p_USARTx->DR = (*p_data & (uint16_t)0x01ff); //only need the 9 bits out of the 2 bytes
						p_USART_Handle->p_Tx_buffer += 2;	//increment by 2 (2 bytes)
						p_USART_Handle->Tx_len -= 2;
					}
					else
					{
						//parity is enabled to either ODD or EVEN, so only 8 bits will be sent
						p_USART_Handle->p_USARTx->DR = *p_USART_Handle->p_Tx_buffer;
						p_USART_Handle->p_Tx_buffer++;		//increment by 1 (1 byte)
						p_USART_Handle->Tx_len--;
					}
				}
				else
				{
					//word length is 8 bits
					if (p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
					{
						//all 8 bits are data, no parity
						p_USART_Handle->p_USARTx->DR = *p_USART_Handle->p_Tx_buffer;
					}
					else
					{
						//only 7 bits are data, parity is enabled
						p_USART_Handle->p_USARTx->DR = (*p_USART_Handle->p_Tx_buffer & 0x7F);
					}
					p_USART_Handle->p_Tx_buffer++;
					p_USART_Handle->Tx_len--;
				}
			}

			if(p_USART_Handle->Tx_len == 0)
			{
				//disable TXEIE(tx buffer empty interrupt)
				p_USART_Handle->p_USARTx->CR1 &= ~(1 <<  USART_CR1_TXEIE);
			}
		}
	}

	/*
	 * check for RXNE flag
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_RXNE) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_RXNEIE) & 1;
	if(temp1 && temp2)
	{
		//the interrupt was cause by RXNE
		if(p_USART_Handle->Rx_state == USART_STATE_BUSY_RX)
		{
			if(p_USART_Handle->Rx_len > 0)
			{
				if(p_USART_Handle->USARTx_config.USART_word_len == USART_WORD_LEN_9BITS)
				{
					//receive 9 bits of data
					if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
					{
						//parity is not used, so all 9 bits are data
						*(uint16_t*)p_USART_Handle->p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint16_t)0x01ff);
						p_USART_Handle->p_Rx_buffer += 2;
						p_USART_Handle->Rx_len -= 2;
					}
					else
					{
						//parity is used, so only 8 bits are data and 1 is parity
						*p_USART_Handle->p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0xff);
						p_USART_Handle->p_Rx_buffer++;
						p_USART_Handle->Rx_len--;
					}
				}
				else
				{
					//receive 8 bits of data
					if(p_USART_Handle->USARTx_config.USART_parity == USART_PARITY_DISABLE)
					{
						//parity is not used, so all 8 bits are data
						*p_USART_Handle->p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0xff);
					}
					else
					{
						//parity is used, so only 7 bits are data and 1 is parity
						*p_USART_Handle->p_Rx_buffer = (p_USART_Handle->p_USARTx->DR & (uint8_t)0x7f);
					}
					p_USART_Handle->p_Rx_buffer++;
					p_USART_Handle->Rx_len--;
				}

				if(p_USART_Handle->Rx_len == 0)
				{
					//disable RXNE(Rx buffer not empty interrupt)
					p_USART_Handle->p_USARTx->CR1 &= ~(1 << USART_CR1_RXNEIE);
					p_USART_Handle->Rx_state = USART_STATE_READY;
					//notify user application
					USART_event_callback(p_USART_Handle, USART_EV_RX_CMPLT);
				}
			}
		}
	}

	/*
	 * check for CTS flag
	 * note: the CTS feature is only applicable for USART, so UART4 and UART5 are not supported
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_CTS) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR3 >> USART_CR3_CTSE) & 1;
	temp3 = (p_USART_Handle->p_USARTx->CR3 >> USART_CR3_CTSIE) & 1;
	if(temp1 && temp2 && temp3)
	{
		//the interrupt was caused by CTS

		//clear CTS flag
		p_USART_Handle->p_USARTx->SR &= ~(1 << USART_SR_CTS);

		//notify user application
		USART_event_callback(p_USART_Handle, USART_EV_CTS);
	}

	/*
	 * check for IDLE flag
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_IDLE) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_IDLEIE) & 1;
	if(temp1 && temp2)
	{
		//the interrupt is caused by IDLE

		//clear IDLE flag
		dummy_byte = p_USART_Handle->p_USARTx->SR;
		dummy_byte = p_USART_Handle->p_USARTx->DR;

		//notify user application
		USART_event_callback(p_USART_Handle, USART_EV_IDLE);
	}

	/*
	 * check for ORE flag
	 */
	temp1 = (p_USART_Handle->p_USARTx->SR >> USART_SR_ORE) & 1;
	temp2 = (p_USART_Handle->p_USARTx->CR1 >> USART_CR1_RXNEIE) & 1;
	if(temp1 && temp2)
	{
		//the interrupt is caused by ORE

		//clear ORE
		dummy_byte = p_USART_Handle->p_USARTx->SR;
		dummy_byte = p_USART_Handle->p_USARTx->DR;

		//notify user application
		USART_event_callback(p_USART_Handle, USART_EV_ORE);
	}

	/*
	 * check for Error flag(FE, NE, ORE)
	 */
	temp1 = p_USART_Handle->p_USARTx->SR;
	temp2 = (p_USART_Handle->p_USARTx->CR3 >> USART_CR3_EIE) & 1;

	if(temp2)
	{
		if( (temp1 >> USART_SR_NF) & 1 )
		{
			//interrupt was caused by NF(noise flag)

			//clear NF
			dummy_byte = p_USART_Handle->p_USARTx->SR;
			dummy_byte = p_USART_Handle->p_USARTx->DR;

			//notify user application
			USART_event_callback(p_USART_Handle, USART_ER_NF);
		}
		else if ( (temp1 >> USART_SR_FE) & 1 )
		{
			//interrupt was caused by FE (framing error)

			//clear FE
			dummy_byte = p_USART_Handle->p_USARTx->SR;
			dummy_byte = p_USART_Handle->p_USARTx->DR;

			//notify user application
			USART_event_callback(p_USART_Handle, USART_ER_FE);
		}
	}
	(void)dummy_byte;
}

/**
 * @brief		This function enables the given USART peripheral and the ACK bit as configured
 *
 * @param		p_USART_Handle address of the USART peripheral
 * @param		enable ENABLE or DISABLE
 *
 * @note		should be called after USART_init (after configuration is done)
 */
void USART_periph_control(USART_Handle_t *p_USART_Handle, uint8_t enable)
{
	if(enable == ENABLE)
	{
		p_USART_Handle->p_USARTx->CR1 |= (1 << USART_CR1_UE);
	}else
	{
		p_USART_Handle->p_USARTx->CR1 &= ~(1 << USART_CR1_UE);
	}
}

/**
 * @brief		This function returns the status of the given flag bit of the USART status register(SR)
 *
 * @param		p_USARTx base address of the USART device
 * @param		flag_bit which SR register to read (1 or 2)
 * @param		the flag bit of the SR register to get status from
 *
 * @return		the status of the given flag bit
 */
uint8_t USART_get_flag_status(USART_reg_t *p_USARTx, uint8_t flag_bit)
{
	return ( (p_USARTx->SR >> flag_bit) & 1 );
}
