/*
 * I2C.c
 *
 *  Created on: Dec 30, 2025
 *      Author: krisko
 */

#include "I2C.h"
//private helper functions
static void I2C_generate_start(I2C_Handle_t *p_I2C_Handle);
static void I2C_execute_addr_phase(I2C_Handle_t *p_I2C_Handle, uint8_t slave_addr, uint8_t read_or_write);
static void I2C_clear_ADDR_flag(I2C_Handle_t *p_I2C_Handle);
static void I2C_master_RXNE_handler(I2C_Handle_t *p_I2C_Handle);
static void I2C_master_TXE_handler(I2C_Handle_t *p_I2C_Handle);

/**
 * @brief		This function enable/disable the clock for the given I2C peripheral
 *
 * @param		p_I2Cx address of I2C peripheral
 * @param		enable ENABLE or DISABLE
 */
void I2C_clock_control(I2C_reg_t *p_I2Cx, uint8_t enable)
{
	if(enable == ENABLE)
	{
		if(p_I2Cx == I2C1)
		{
			I2C1_PCLK_EN();
		}
		else if(p_I2Cx == I2C2)
		{
			I2C2_PCLK_EN();
		}
		else if(p_I2Cx == I2C3)
		{
			I2C3_PCLK_EN();
		}
	}else
	{
		if(p_I2Cx == I2C1)
		{
			I2C1_PCLK_DI();
		}
		else if(p_I2Cx == I2C2)
		{
			I2C2_PCLK_DI();
		}
		else if(p_I2Cx == I2C3)
		{
			I2C3_PCLK_DI();
		}
	}
}

/**
 * @brief		This function configures the given I2C peripheral
 *
 * @param		p_I2C_Handle address of I2C Handle for the peripheral
 *
 * @note		for system clock source, only HSI and HSE are considered, PLL and PLLR are not considered,
 *              only 7 bit addresses are considered
 */
void I2C_init(I2C_Handle_t *p_I2C_Handle)
{
	uint32_t temp;
	uint32_t PCLK1_freq_hz = RCC_get_pclk1();

	//enable peripheral clock
	I2C_clock_control(p_I2C_Handle->p_I2Cx, ENABLE);


	//configure clock frequency
	uint32_t PCLK1_freq_Mhz = PCLK1_freq_hz / 1000000;	//FREQ only support Mhz
	temp = p_I2C_Handle->p_I2Cx->CR2;
	temp &= ~(0b11111 << I2C_CR2_FREQ);
	temp |= (PCLK1_freq_Mhz << I2C_CR2_FREQ);
	p_I2C_Handle->p_I2Cx->CR2 = temp;

	//configure device address mode
	temp = p_I2C_Handle->p_I2Cx->OAR1;
	temp &= ~(1 << I2C_OAR_ADDMODE);		//7 bit address

	//configure device address
	temp &= ~(0x7F << 1);
	temp |= ( (p_I2C_Handle->I2Cx_config.I2C_device_addr & 0x7F) << 1);

	temp |= (1 << 14);	//keep 14th bit of OAR 1, as required by data sheet
	p_I2C_Handle->p_I2Cx->OAR1 = temp;

	//configure clock control register
	uint32_t CCR;
	temp = p_I2C_Handle->p_I2Cx->CCR;
	uint32_t clock_speed = p_I2C_Handle->I2Cx_config.I2C_CLK_speed;
	if( clock_speed <= I2C_CLK_SPEED_SM)
	{
		//standard mode
		temp &= ~(1 << I2C_CCR_FS);		//set mode to SM
		//calculate CCR
		CCR = PCLK1_freq_hz / (2 * clock_speed);
	}
	else
	{
		//fast mode
		temp |= (1 << I2C_CCR_FS);		//set mode to FM
		//set FR duty cycle
		uint8_t duty = p_I2C_Handle->I2Cx_config.I2C_FM_duty_cycle;
		if(duty)	//t(low)/t(high) = 16/9
		{
			//set DUTY to 1
			temp |= (duty << I2C_CCR_DUTY);
			//calculate CCR
			CCR = PCLK1_freq_hz / (25 * clock_speed);
		}
		else		//t(low)/t(high) = 2
		{
			//set DUTY to 0
			temp &= ~(1 << I2C_CCR_DUTY);
			//calculate CCR
			 CCR = PCLK1_freq_hz / (3 * clock_speed);

		}
	}
	temp &= ~(0xFFF << I2C_CCR_CCR);
	temp |= ((CCR & 0xFFF) << I2C_CCR_CCR);
	p_I2C_Handle->p_I2Cx->CCR = temp;

	//configure T(rise)
	temp = p_I2C_Handle->p_I2Cx->TRISE;
	temp &= ~(0x3F);
	if(clock_speed <= I2C_CLK_SPEED_SM)
	{
		//standard mode, T(rise) max = 1000ns
		temp |= (PCLK1_freq_hz / 1000000) + 1;		//add 1, as specified in data sheet
	}
	else
	{
		//fast mode, T(rise) max = 300ns
		temp |= ((PCLK1_freq_hz * 300) / 1000000000) + 1;
	}
	p_I2C_Handle->p_I2Cx->TRISE = (temp & 0x3F);
}


/**
 * @brief		This function disables the clock of the given I2C peripheral
 *
 * @param		p_I2Cx address of the I2C peripheral
 */
void I2C_deinit(I2C_reg_t *p_I2Cx){
	if(p_I2Cx == I2C1)
	{
		I2C1_REG_RESET();
	}
	else if(p_I2Cx == I2C2)
	{
		I2C2_REG_RESET();
	}
	else if(p_I2Cx == I2C3)
	{
		I2C3_REG_RESET();
	}
}

/**
 * @brief		This function sends the given data to the slave at the given address
 *
 * @param		p_I2C_Handle address of the I2C peripheral
 * @param		p_Tx_buffer address of the Tx buffer
 * @param		len how many bytes of data to send
 * @param		slave_addr slave address
 * @param		RS_enable enable or disable repeated start (I2C_RS_enable or I2C_SR_DISABLE)
 *
 * @note		this is an blocking based call
 */
void I2C_master_send(I2C_Handle_t *p_I2C_Handle, uint8_t *p_Tx_buffer, uint32_t len, uint8_t slave_addr, uint8_t RS_enable)
{
	//generate starting condition
	I2C_generate_start(p_I2C_Handle);
	//check SB flag in SR1 to confirm that start condition is generated
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1,  I2C_SR1_SB) == 0);
	//send the address with the r/w bit set to write(0) to slave
	I2C_execute_addr_phase(p_I2C_Handle, slave_addr, WRITE);
	//check ADDR flag in SR1 to confirm address is sent
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1, I2C_SR1_ADDR) == 0);
	//clear the ADDR flag to release SCL stretch(pulled to LOW)
	I2C_clear_ADDR_flag(p_I2C_Handle);
	//send data
	while(len > 0)
	{
		while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1, I2C_SR1_TxE) == 0);
		p_I2C_Handle->p_I2Cx->DR = *p_Tx_buffer;
		p_Tx_buffer++;
		len--;
	}
	//wait until TxE=1 (data register is empty) and BTF=1 (byte transfer is finished) to close communication
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1, I2C_SR1_TxE) == 0);
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1, I2C_SR1_BTF) == 0);
	//generate stop condition if repeated start disabled
	if(RS_enable == I2C_RS_DISABLE)
		I2C_generate_stop(p_I2C_Handle);
}

/**
 * @brief		This function receives the given data from the slave at the given address
 *
 * @param		p_I2C_Handle address of the I2C peripheral
 * @param		p_Rx_buffer address of the Rx buffer
 * @param		len how many bytes of data to receive
 * @param		slave_addr slave address
 * @param		RS_enable enable or disable repeated start (I2C_RS_enable or I2C_SR_DISABLE)
 *
 * @note		this is an blocking based call
 */
void I2C_master_receive(I2C_Handle_t *p_I2C_Handle, uint8_t *p_Rx_buffer, uint32_t len, uint8_t slave_addr, uint8_t RS_enable)
{
	//generate start condition
	I2C_generate_start(p_I2C_Handle);
	//check SB flag in SR1 to confirm that start condition is generated
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1,  I2C_SR1_SB) == 0);
	//send the address  to slave with the r/w bit set to write(0)
	I2C_execute_addr_phase(p_I2C_Handle, slave_addr, READ);
	//check ADDR flag in SR1 to confirm address is sent
	while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1, I2C_SR1_ADDR) == 0);

	//receiving only one byte of data
	/*
	 * note:
	 * 		for len == 1, must disable ACK BEFORE clearing ADDR flag. Clearing ADDR releases SCL, and byte transfer
	 * 		starts immediately. ACK/NACK will be sent on the 9th clock (end of byte). So if ACK bit is not disabled
	 * 		before ADDR clear, hardware will ACK, causing slave to send an extra unwanted byte.
	 *
	 *		for len >= 2, can clear ADDR flag as usual and disable ACK and generate STOP when len == 2,
	 *		so the second last byte gets ACK and last byte gets NACK.
	 *
	 */
	if(len == 1)
	{
		//disable ACK
		I2C_manage_acking(p_I2C_Handle, DISABLE);
		//clear the ADDR flag to release SCL stretch(pulled to LOW), so slave start transmitting the data
		I2C_clear_ADDR_flag(p_I2C_Handle);
		//wait until RXNE is set to 1
		while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1,  I2C_SR1_RxNE) == 0);
		//generate stop condition
		if(RS_enable == I2C_RS_DISABLE)
			I2C_generate_stop(p_I2C_Handle);
		//read data from DR to Rx buffer
		*p_Rx_buffer = p_I2C_Handle->p_I2Cx->DR;
	}
	else
	{
		//clear ADDR flag so data reception begins
		I2C_clear_ADDR_flag(p_I2C_Handle);
		while(len > 0)
		{
			//wait until RXNE is set to 1, ready to read
			while(I2C_get_flag_status(p_I2C_Handle->p_I2Cx, 1,  I2C_SR1_RxNE) == 0);

			if(len == 2)		//if only 2 bytes are left to receive
			{
				//disable ACK
				I2C_manage_acking(p_I2C_Handle, DISABLE);
				//generate stop condition
				if(RS_enable == I2C_RS_DISABLE)
					I2C_generate_stop(p_I2C_Handle);
			}

			//read data from DR to Rx buffer
			*p_Rx_buffer = p_I2C_Handle->p_I2Cx->DR;
			len--;
			p_Rx_buffer++;

		}
	}
	//re-enalbe ACK if configured
	if(p_I2C_Handle->I2Cx_config.I2C_ACK_control == I2C_ACK_ENABLE)
	{
		I2C_manage_acking(p_I2C_Handle, ENABLE);
	}
}

/**
 * @brief		This function sends data to master
 *
 * @param		p_I2Cx address of the I2C peripheral of master
 * @param		data to send
 */
void I2C_slave_send(I2C_reg_t *p_I2Cx, uint8_t data)
{
	p_I2Cx->DR = data;
}

/**
 * @brief		This function reads data sent from master
 *
 * @param		p_I2Cx address of the I2C peripheral of master
 *
 * @return		byte of data received
 */
uint8_t I2C_slave_receive(I2C_reg_t *p_I2Cx)
{
	return (uint8_t)p_I2Cx->DR;
}

/**
 * @brief		This function start the sending process by setting START condition and enabling interrupts
 *
 * @param		p_I2C_Handle address of the I2C peripheral
 * @param		p_Tx_buffer address of the Rx buffer
 * @param		len how many bytes of data to receive
 * @param		slave_addr slave address
 * @param		RS_enable enable or disable repeated start (I2C_RS_enable or I2C_SR_DISABLE)
 *
 * @return		the state of the I2C peripheral when entering the function, not updated if state is READY entered
 *
 * @note		this function only initiates the process, the actually reception of data is done in handlers
 */
uint8_t I2C_master_send_IT(I2C_Handle_t *p_I2C_Handle, uint8_t *p_Tx_buffer, uint32_t len, uint8_t slave_addr, uint8_t RS_enable)
{
	uint8_t state = p_I2C_Handle->TxRxstate;

	if((state != I2C_STATE_BUSY_RX) && (state != I2C_STATE_BUSY_TX))
	{
		//save the information to I2C handler
		p_I2C_Handle->p_Tx_buffer = p_Tx_buffer;
		p_I2C_Handle->Tx_len = len;
		p_I2C_Handle->TxRxstate = I2C_STATE_BUSY_TX;
		p_I2C_Handle->repeated_start = RS_enable;
		p_I2C_Handle->slave_addr = slave_addr;

		//generate start condition
		I2C_generate_start(p_I2C_Handle);

		//enable ITBUFEN(buffer interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITBUFEN);
		//enable ITEVTEN(event interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITEVTEN);
		//enable ITERREN(error interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITERREN);
	}
	return state;

}

/**
 * @brief		This function start the receiving process by setting START condition and enabling interrupts
 *
 * @param		p_I2C_Handle address of the I2C peripheral
 * @param		p_Rx_buffer address of the Rx buffer
 * @param		len how many bytes of data to receive
 * @param		slave_addr slave address
 * @param		RS_enable enable or disable repeated start (I2C_RS_enable or I2C_SR_DISABLE)
 *
 * @return		the state of the I2C peripheral when entering the function, not updated if state is READY entered
 *
 * @note		this function only initiates the process, the actually reception of data is done in handlers
 */
uint8_t I2C_master_receive_IT(I2C_Handle_t *p_I2C_Handle, uint8_t *p_Rx_buffer, uint32_t len, uint8_t slave_addr, uint8_t RS_enable)
{
	uint8_t state = p_I2C_Handle->TxRxstate;

	if((state != I2C_STATE_BUSY_RX) && (state != I2C_STATE_BUSY_TX))
	{
		//save the information to I2C handler
		p_I2C_Handle->p_Rx_buffer = p_Rx_buffer;
		p_I2C_Handle->Rx_len = len;
		p_I2C_Handle->Rx_size = len;
		p_I2C_Handle->TxRxstate = I2C_STATE_BUSY_RX;
		p_I2C_Handle->repeated_start = RS_enable;
		p_I2C_Handle->slave_addr = slave_addr;


		//generate start condition
		I2C_generate_start(p_I2C_Handle);

		//enable ITBUFEN(buffer interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITBUFEN);
		//enable ITEVTEN(event interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITEVTEN);
		//enable ITERREN(error interrupt enable)
		p_I2C_Handle->p_I2Cx->CR2 |= (1 << I2C_CR2_ITERREN);
	}
	return state;
}

/**
 * @brief		This function enable/disable interrupt for the given peripheral
 *
 * @param		IRQ_num the IRQ number to enable/disable
 * @param		enable ENABLE or DISABLE the IRQ
 */
void I2C_IRQ_config(uint8_t IRQ_num, uint8_t enable)
{
	//enable the IRQ
	if(enable == ENABLE)
	{
		if(IRQ_num < 32)
		{
			//enable ISER0
			*NVIC_ISER0 |= (1 << IRQ_num);
		}
		else if(IRQ_num >= 32 && IRQ_num < 64)
		{
			//enable ISER1
			*NVIC_ISER1 |= (1 << (IRQ_num % 32));
		}
		else if(IRQ_num >= 64 && IRQ_num < 96){
			//enable ISER2
			*NVIC_ISER2 |= (1 << (IRQ_num % 64));
		}
		else if (IRQ_num >= 96 && IRQ_num < 128)
		{
			//enable ISER3
			*NVIC_ISER3 |= (1 << (IRQ_num % 96));
		}
	}else{ //disable the IRQ
		if(IRQ_num < 32)
		{
			//enable ICER0
			*NVIC_ICER0 |= (1 << IRQ_num);
		}
		else if(IRQ_num >= 32 && IRQ_num < 64)
		{
			//enable ICER1
			*NVIC_ICER1 |= (1 << (IRQ_num % 32));
		}
		else if(IRQ_num >= 64 && IRQ_num < 96){
			//enable ICER2
			*NVIC_ICER2 |= (1 << (IRQ_num % 64));
		}
		else if (IRQ_num >= 96 && IRQ_num < 128)
		{
			//enable ICER3
			*NVIC_ICER3 |= (1 << (IRQ_num % 96));
		}
	}

}

/**
 * @brief		This function enable/disable the GPIO pin as given
 *
 * @param		IRQ_num IRQ number of the peripheral to set priority
 * @param		IRQ_priority priority value to set the IRQ to
 */
void I2C_set_priority(uint8_t IRQ_num, uint8_t IRQ_priority)
{
	//set priority
	uint8_t iprx = IRQ_num / 4;						//which IRQ register, each IPR register only contain 4 interrupts (1 byte apart)
	uint8_t iprx_section = IRQ_num % 4;				//which interrupt(byte) within the IPR register
	uint8_t shift_amount = (8 * iprx_section) + 4; 	//add 4 because the upper 4 bits are the preemptive priority and the lower 4 are the subpriority

	*(NVIC_IPR_BASEADDR + iprx) |= (IRQ_priority << shift_amount); //NVIC_IPR_BASEADDR is uin32_t pointer so adding the iprx will be 4 bytes apart
}

/**
 * @brief		This function handles I2C event interrupts (SB, ADDR, BTF, STOPF, TXE, RXNE)
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
void I2C_EV_IRQ_handling(I2C_Handle_t *p_I2C_Handle)
{
	uint8_t temp1 = ( (p_I2C_Handle->p_I2Cx->CR2 >> I2C_CR2_ITBUFEN) & 1 );
	uint8_t temp2 = ( (p_I2C_Handle->p_I2Cx->CR2 >> I2C_CR2_ITEVTEN) & 1 );

	//handle interrupt generated by SB
	//note: this will only execute for master mode. In slave mode, SB is always 0
	uint8_t temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_SB) & 1 );
	if(temp2 && temp3)
	{
		//when SB is set, it means address phase should be sent
		if(p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_RX)
		{
			I2C_execute_addr_phase(p_I2C_Handle, p_I2C_Handle->slave_addr, READ);
		}
		else if (p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_TX)
		{
			I2C_execute_addr_phase(p_I2C_Handle, p_I2C_Handle->slave_addr, WRITE);
		}
	}

	//handle interrupt generated by ADDR
	temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_ADDR) & 1 );
	if(temp2 && temp3)
	{
		//disable ACK first before clearing ADDR for master receive
		if( (p_I2C_Handle->p_I2Cx->SR2 >> I2C_SR2_MSL) & 1)	//device in master mode
		{
			if(p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_RX)	//device is receiving
			{
				if(p_I2C_Handle->Rx_size == 1)
				{
					//disable ACK
					p_I2C_Handle->p_I2Cx->CR1 &= ~(1 << I2C_CR1_ACK);

					//clear the ADDR flag
					I2C_clear_ADDR_flag(p_I2C_Handle);
				}
			}
			else
			{
				//device is sending data, so address phase was successful sent and received,
				//now clear ADDR flag so transmission continues
				I2C_clear_ADDR_flag(p_I2C_Handle);
			}
		}
		else
		{
			//device is slave mode
			I2C_clear_ADDR_flag(p_I2C_Handle);
		}

	}

	//handle interrupt generated by BTF
	temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_BTF) & 1 );
	if(temp2 && temp3)
	{
		if(p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_TX)
		{
			if((p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_TxE) & 1)
			{
				//both BTF = 1 and TXE = 1

				//generate stop condition if all data was sent and repeated start disabled
				if( (p_I2C_Handle->Tx_len == 0) && (p_I2C_Handle->repeated_start == I2C_RS_DISABLE)	)
				{
					I2C_generate_stop(p_I2C_Handle);
				}

				//reset Tx related elements of handle structure
				I2C_close_send(p_I2C_Handle);

				//notify user application that transmission completed
				I2C_event_callback(p_I2C_Handle, I2C_EV_TX_CMPLT);
			}
		}
	}

	//handle interrupt generated by STOPF
	//note: this will only execute for slave mode, since master mode will not receive a stop flag, only slave
	//receives notice to stop when master is receiving
	temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_STOPF) & 1 );
	if(temp2 && temp3)
	{
		//clear STOPF, read SR1(already done above) then write to CR1
		p_I2C_Handle->p_I2Cx->CR1 |= 0;	//a dummy write

		//notify user application that stoop is detected
		I2C_event_callback(p_I2C_Handle, I2C_EV_STOP);
	}


	//handle interrupt generated by TXE
	temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_TxE) & 1 );
	if(temp1 && temp2 && temp3)
	{
		//TXE flag is set, do data transmission
		if( (p_I2C_Handle->p_I2Cx->SR2 >> I2C_SR2_MSL) & 1 ) //send data only if device is master
		{
			if(p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_TX) //send only if the peripheral is transmitting
			{
				//send the data
				I2C_master_TXE_handler(p_I2C_Handle);
			}
		}
		else  //device is slave mode
		{
			if( (p_I2C_Handle->p_I2Cx->SR2 >> I2C_SR2_TRA) & 1)	//ensure device is in transmitter mode
			{
				//notify user applicator
				I2C_event_callback(p_I2C_Handle, I2C_EV_DATA_REQ);
			}
		}
	}

	//handle interrupt generated by RXNE
	temp3 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_RxNE) & 1 );
	if(temp1 && temp2 && temp3)
	{
		if( (p_I2C_Handle->p_I2Cx->SR2 >> I2C_SR2_MSL) & 1)		//device in master mode
		{
			//RXNE flag is set, do data reception
			if(p_I2C_Handle->TxRxstate == I2C_STATE_BUSY_RX)	//device is receiving data
			{
				I2C_master_RXNE_handler(p_I2C_Handle);
			}
		}
		else  //device is slave mode
		{
			if( ((p_I2C_Handle->p_I2Cx->SR2 >> I2C_SR2_TRA) & 1) == 0)	//ensure device is in receiver mode
			{
				//notify user applicator
				I2C_event_callback(p_I2C_Handle, I2C_EV_DATA_REC);
			}
		}
	}
}


/**
 * @brief		This function handles I2C error interrupts (BERR, ARLO, AF, OVR, PECERR, TIMEOUT, SMBALERT)
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
void I2C_ER_IRQ_handling(I2C_Handle_t *p_I2C_Handle)
{
	uint8_t temp1 = ( (p_I2C_Handle->p_I2Cx->CR2 >> I2C_CR2_ITERREN) & 1 );

	//handle error generated by BERR
	uint8_t temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_BERR) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_BERR);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_BERR);
	}

	//handle error generated by ARLO
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_ARLO) & 1 );
	if(temp1 & temp2)
	{
		//clear ARLO
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_ARLO);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_ARLO);
	}

	//handle error generated by AF
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_AF) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_AF);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_AF);
	}

	//handle error generated by OVR
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_OVR) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_OVR);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_OVR);
	}

	//handle error generated by PECERR
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_PECERR) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_PECERR);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_PECERR);
	}

	//handle error generated by TIMEOUT
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_TIMEOUT) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_TIMEOUT);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_TIMEOUT);
	}

	//handle error generated by SMBALERT
	temp2 = ( (p_I2C_Handle->p_I2Cx->SR1 >> I2C_SR1_SMBALERT) & 1 );
	if(temp1 & temp2)
	{
		//clear BERR
		p_I2C_Handle->p_I2Cx->SR1 &= ~(1 << I2C_SR1_SMBALERT);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_ER_SMBALERT);
	}

}

/**
 * @brief		This function loads the next byte into DR when TXE is set during master transmission
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 *
 * @note		This function is called by I2C_EV_IRQ_handling
 */
static void I2C_master_TXE_handler(I2C_Handle_t *p_I2C_Handle)
{
	if(p_I2C_Handle->Tx_len > 0) //send only if there are more bytes to send
	{
		//load data into DR
		p_I2C_Handle->p_I2Cx->DR = *p_I2C_Handle->p_Tx_buffer;
		p_I2C_Handle->p_Tx_buffer++;
		p_I2C_Handle->Tx_len--;
	}
}
/**
 * @brief		This function reads the next byte from DR when RXNE is set during master reception,
 * 				managing ACK/NACK and stop generation as the transfer nears completion
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 *
 * @note		This function is called by I2C_EV_IRQ_handling
 */
static void I2C_master_RXNE_handler(I2C_Handle_t *p_I2C_Handle)
{
	if(p_I2C_Handle->Rx_size == 1)
	{
		*p_I2C_Handle->p_Rx_buffer = p_I2C_Handle->p_I2Cx->DR;
		p_I2C_Handle->Rx_len--;
	}

	if (p_I2C_Handle->Rx_size > 1)
	{

		if( p_I2C_Handle->Rx_len == 2)
		{
			//clear ACK
			I2C_manage_acking(p_I2C_Handle, DISABLE);
		}

		*p_I2C_Handle->p_Rx_buffer = p_I2C_Handle->p_I2Cx->DR;
		p_I2C_Handle->Rx_len--;
		p_I2C_Handle->p_Rx_buffer++;
	}

	if(p_I2C_Handle->Rx_len == 0)
	{
		//generate stop condition
		if(p_I2C_Handle->repeated_start == I2C_RS_DISABLE)
			I2C_generate_stop(p_I2C_Handle);
		//close the I2C rx
		I2C_close_receive(p_I2C_Handle);
		//notify user application
		I2C_event_callback(p_I2C_Handle, I2C_EV_RX_CMPLT);

	}
}

/**
 * @brief		This function disables TXEIE/ITEVTEN, resets the Tx-related I2C Handle fields, and
 * 				re-enables ACK if configured
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
void I2C_close_send(I2C_Handle_t *p_I2C_Handle)
{
	//disable ITBUFEN(buffer interrupt enable)
	p_I2C_Handle->p_I2Cx->CR2 &= ~(1 << I2C_CR2_ITBUFEN);
	//disable ITEVTEN(event interrupt enable)
	p_I2C_Handle->p_I2Cx->CR2 &= ~(1 << I2C_CR2_ITEVTEN);

	//clear I2C handler variables
	p_I2C_Handle->TxRxstate = I2C_STATE_READY;
	p_I2C_Handle->p_Tx_buffer = NULL;
	p_I2C_Handle->Tx_len = 0;

	//enable ACK if confugured
	if(p_I2C_Handle->I2Cx_config.I2C_ACK_control == I2C_ACK_ENABLE)
		I2C_manage_acking(p_I2C_Handle, ENABLE);
}

/**
 * @brief		This function disables RXNEIE/ITEVTEN, resets the Rx-related I2C Handle fields, and
 * 				re-enables ACK if configured
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
void I2C_close_receive(I2C_Handle_t *p_I2C_Handle)
{
	//disable ITBUFEN(buffer interrupt enable)
	p_I2C_Handle->p_I2Cx->CR2 &= ~(1 << I2C_CR2_ITBUFEN);
	//disable ITEVTEN(event interrupt enable)
	p_I2C_Handle->p_I2Cx->CR2 &= ~(1 << I2C_CR2_ITEVTEN);

	//clear I2C handler variables
	p_I2C_Handle->TxRxstate = I2C_STATE_READY;
	p_I2C_Handle->p_Rx_buffer = NULL;
	p_I2C_Handle->Rx_len = 0;
	p_I2C_Handle->Rx_size = 0;

	//enable ACK if confugured
	if(p_I2C_Handle->I2Cx_config.I2C_ACK_control == I2C_ACK_ENABLE)
		I2C_manage_acking(p_I2C_Handle, ENABLE);
}

/**
 * @brief		This function enables the given I2C peripheral and the ACK bit as configured
 *
 * @param		p_I2C_Handle address of the I2C peripheral
 * @param		enable ENABLE or DISABLE
 *
 * @note		should be called after I2C_init (after configuration is done)
 */
void I2C_periph_control(I2C_Handle_t *p_I2C_Handle, uint8_t enable)
{
	if(enable == ENABLE)
	{
		p_I2C_Handle->p_I2Cx->CR1 |= (1 << I2C_CR1_PE);
	}else
	{
		p_I2C_Handle->p_I2Cx->CR1 &= ~(1 << I2C_CR1_PE);
	}
	//enable or disable ACK bit accordingly as it should only be enable after PE is set
	if(p_I2C_Handle->I2Cx_config.I2C_ACK_control == I2C_ACK_ENABLE )
	{
		I2C_manage_acking(p_I2C_Handle, ENABLE);
	}
	else
	{
		I2C_manage_acking(p_I2C_Handle, DISABLE);
	}
}

/**
 * @brief		This function returns the status of the given flag bit of the I2C status register(SR)
 *
 * @param		p_I2Cx base address of the I2C device
 * @param		SR which SR register to read (1 or 2)
 * @param		flag_bit the flag bit of the SR register to get status from
 *
 * @return		the status of the given flag bit
 */
uint8_t I2C_get_flag_status(I2C_reg_t *p_I2Cx, uint8_t SR, uint8_t flag_bit)
{
	if(SR == 1)
	{
		return ( (p_I2Cx->SR1 >> flag_bit) & 1 );
	}
	else
	{
		return ( (p_I2Cx->SR2 >> flag_bit) & 1 );
	}
}

/**
 * @brief		This function disable or enable the ACK bit of I2C_CR1 register
 *
 * @param		p_I2C_Handle base address of the I2C device
 * @param		enable ENABLE or DISABLE
 *
 * @return		the status of the given flag bit
 */
void I2C_manage_acking(I2C_Handle_t *p_I2C_Handle, uint8_t enable)
{
	if(enable == ENABLE)
	{
		p_I2C_Handle->p_I2Cx->CR1 |= (1 << I2C_CR1_ACK);
	}
	else if (enable == DISABLE)
	{
		p_I2C_Handle->p_I2Cx->CR1 &= ~(1 << I2C_CR1_ACK);
	}
}

/*-----private helper functions-----*/

/**
 * @brief		This function sets the START bit in CR1 to generate a start condition
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
static void I2C_generate_start(I2C_Handle_t *p_I2C_Handle)
{
	p_I2C_Handle->p_I2Cx->CR1 |= (1 << I2C_CR1_START);
}

/**
 * @brief		This function shifts the slave address left by 1 and sets the r/w bit, then writes it to DR
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 * @param		slave_addr 7-bit address of the target slave
 * @param		read_or_write WRITE or READ, sets the r/w bit accordingly
 */
static void I2C_execute_addr_phase(I2C_Handle_t *p_I2C_Handle, uint8_t slave_addr, uint8_t read_or_write)
{
	slave_addr = slave_addr << 1;
	if(read_or_write == WRITE)
	{
		slave_addr &= ~(1);		//r/w bit = 0
	}
	else if(read_or_write == READ)
	{
		slave_addr |= 1;		//r/w bit = 1
	}
	p_I2C_Handle->p_I2Cx->DR = slave_addr;
}

/**
 * @brief		This function clears the ADDR flag by reading SR1 then SR2, releasing SCL stretch
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
static void I2C_clear_ADDR_flag(I2C_Handle_t *p_I2C_Handle)
{
	//clear the ADDR flag by reading SR1 and SR2
	uint32_t dummy = p_I2C_Handle->p_I2Cx->SR1;
	dummy = p_I2C_Handle->p_I2Cx->SR2;
	(void)dummy;
}

/**
 * @brief		This function sets the STOP bit in CR1 to generate a stop condition
 *
 * @param		p_I2C_Handle address of the I2C Handle structure
 */
void I2C_generate_stop(I2C_Handle_t *p_I2C_Handle)
{
	p_I2C_Handle->p_I2Cx->CR1 |= (1 << I2C_CR1_STOP);
}
