#include "RFM98W.h"
#include <string.h>
#include <stdlib.h>

#define RF_MAX_CLIENTS		16
#define RF_RX_QUEUE_SIZE	16
#define RF_TX_QUEUE_SIZE	4

static RF_Message_t RxQueue[RF_RX_QUEUE_SIZE];
static RF_Message_t TxQueue[RF_TX_QUEUE_SIZE];

RF_Client_t RFClients[RF_MAX_CLIENTS];

typedef enum ModemConfigChoice_e
{
	Bw125Cr45Sf128 = 0,	///< Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Default medium range
	Bw500Cr45Sf128,		///< Bw = 500 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Fast+short range
	Bw31_25Cr48Sf512,	///< Bw = 31.25 kHz, Cr = 4/8, Sf = 512chips/symbol, CRC on. Slow+long range
	Bw125Cr48Sf4096,	///< Bw = 125 kHz, Cr = 4/8, Sf = 4096chips/symbol, CRC on. Slow+long range
} ModemConfigChoice_t;

typedef struct ModemConfig_s
{
	uint8_t    reg_1d;   ///< Value for register RH_RF98_REG_1D_MODEM_CONFIG1
	uint8_t    reg_1e;   ///< Value for register RH_RF98_REG_1E_MODEM_CONFIG2
	uint8_t    reg_26;   ///< Value for register RH_RF98_REG_26_MODEM_CONFIG3
} ModemConfig_t;

const ModemConfig_t MODEM_CONFIG_TABLE[] =
{
    // 1d,	1e,		26
    { 0x72,	0x74,	0x00}, // Bw125Cr45Sf128 (the chip default)
    { 0x92,	0x74,	0x00}, // Bw500Cr45Sf128
    { 0x48,	0x94,	0x00}, // Bw31_25Cr48Sf512
    { 0x78,	0xc4,	0x00}, // Bw125Cr48Sf4096
};

RFContext_t RFContext;

/// Array of the last seen sequence number indexed by node address that sent it
/// It is used for duplicate detection. Duplicated messages are re-acknowledged when received 
/// (this is generally due to lost ACKs, causing the sender to retransmit, even though we have already
/// received that message)
static uint8_t seenIds[256];

static void powerOff(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	HAL_GPIO_WritePin(RFPWR_PORT, RFPWR_PIN, GPIO_PIN_RESET);

	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	GPIO_InitStruct.Pin = RFRST_PIN;
	HAL_GPIO_Init(RFRST_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = RFNSS_PIN;
	HAL_GPIO_Init(RFNSS_PORT, &GPIO_InitStruct);
}

static void powerOn(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;

	GPIO_InitStruct.Pin = RFRST_PIN;
	HAL_GPIO_Init(RFRST_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = RFNSS_PIN;
	HAL_GPIO_Init(RFNSS_PORT, &GPIO_InitStruct);

	HAL_GPIO_WritePin(RFPWR_PORT, RFPWR_PIN, GPIO_PIN_SET);
}

static void nss_Low(void)		{	HAL_GPIO_WritePin(RFNSS_PORT, RFNSS_PIN, GPIO_PIN_RESET);	}
static void nss_High(void)		{	HAL_GPIO_WritePin(RFNSS_PORT, RFNSS_PIN, GPIO_PIN_SET);		}
GPIO_PinState RFMISO_Read(void)		{	return HAL_GPIO_ReadPin(RFSPI_PORT, RFMISO_PIN);	}

SPI_HandleTypeDef hspi1;

void RF_SPI_Off(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	GPIO_InitStruct.Pin = RFRST_PIN;
	HAL_GPIO_Init(RFRST_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = RFNSS_PIN;
	HAL_GPIO_Init(RFNSS_PORT, &GPIO_InitStruct);
	
	GPIO_InitStruct.Pin = (RFSCK_PIN | RFMISO_PIN | RFMOSI_PIN);
	HAL_GPIO_Init(RFSPI_PORT, &GPIO_InitStruct);
}

HAL_StatusTypeDef RF_SPI_On(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	GPIO_InitStruct.Pin = RF_DIO0_PIN;
	HAL_GPIO_Init(RF_DIO0_PORT, &GPIO_InitStruct);

	HAL_NVIC_SetPriority(RF_DIO0_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(RF_DIO0_IRQn);

	nss_High();
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStruct.Pin = RFNSS_PIN;
	HAL_GPIO_Init(RFNSS_PORT, &GPIO_InitStruct);

	hspi1.Instance = SPI1;
	hspi1.Init.Mode = SPI_MODE_MASTER;
	hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi1.Init.NSS = SPI_NSS_SOFT;
	hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
	hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi1.Init.TIMode = SPI_TIMODE_DISABLED;
	hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLED;
	return HAL_SPI_Init(&hspi1);
}

HAL_StatusTypeDef spiWrite(uint8_t reg, uint8_t data)
{
	HAL_StatusTypeDef status;
	uint8_t buffer[2];
	buffer[0] = reg | RH_SPI_WRITE_MASK;
	buffer[1] = data;

	ATOMIC_BLOCK_START;
	nss_Low();
	status = HAL_SPI_Transmit(&hspi1, buffer, 2, 5);
	nss_High();
	ATOMIC_BLOCK_END;

	return status;
}

HAL_StatusTypeDef spiBurstWrite(uint8_t reg, const uint8_t * src, uint8_t len)
{
	HAL_StatusTypeDef status;
	uint8_t buffer[1];
	buffer[0] = reg | RH_SPI_WRITE_MASK;

	ATOMIC_BLOCK_START;
	nss_Low();
	status = HAL_SPI_Transmit(&hspi1, buffer, 1, 5);
	if (status == HAL_OK)
		status = HAL_SPI_Transmit(&hspi1, (uint8_t *)src, len, 100);
	nss_High();
	ATOMIC_BLOCK_END;

	return status;
}

HAL_StatusTypeDef spiRead(uint8_t reg, uint8_t *data)
{
	HAL_StatusTypeDef status;
	uint8_t buffer[1];
	buffer[0] = reg & ~RH_SPI_WRITE_MASK;

	ATOMIC_BLOCK_START;
	nss_Low();
	status = HAL_SPI_Transmit(&hspi1, buffer, 1, 5);
	if (status == HAL_OK)
		status = HAL_SPI_Receive(&hspi1, data, 1, 5);
	nss_High();
	ATOMIC_BLOCK_END;

	if (status != HAL_OK)
		*data = 0;

	return status;
}

HAL_StatusTypeDef spiBurstRead(uint8_t reg, uint8_t* dest, uint8_t len)
{
	HAL_StatusTypeDef status;
	uint8_t buffer[1];
	buffer[0] = reg & ~RH_SPI_WRITE_MASK;

    ATOMIC_BLOCK_START;
	nss_Low();
	status = HAL_SPI_Transmit(&hspi1, buffer, 1, 10);
	if (status == HAL_OK)
		status = HAL_SPI_Receive(&hspi1, dest, len, 100);
    nss_High();
    ATOMIC_BLOCK_END;

	if (status != HAL_OK)
		memset(dest, 0, len);

    return status;
}

HAL_StatusTypeDef RF_SetModeIdle(void)
{
	HAL_StatusTypeDef status = HAL_OK;
	if (RFContext.Mode != RHModeIdle)
    {
		status = spiWrite(RH_RF98_REG_01_OP_MODE, RH_RF98_MODE_STDBY);
		RFContext.Mode = RHModeIdle;
    }
	return status;
}

// Sets registers from a canned modem configuration structure
HAL_StatusTypeDef setModemRegisters(const ModemConfig_t * config)
{
	if (HAL_OK == spiWrite(RH_RF98_REG_1D_MODEM_CONFIG1, config->reg_1d)
	&&	HAL_OK == spiWrite(RH_RF98_REG_1E_MODEM_CONFIG2, config->reg_1e)
	&&	HAL_OK == spiWrite(RH_RF98_REG_26_MODEM_CONFIG3, config->reg_26)
		)
	{
		return HAL_OK;
	}
	return HAL_ERROR;
}

// Set one of the canned FSK Modem configs
// Returns true if its a valid choice
HAL_StatusTypeDef setModemConfig(ModemConfigChoice_t index)
{
	if ((int)index > (sizeof(MODEM_CONFIG_TABLE) / sizeof(ModemConfig_t)))
	{
		return HAL_ERROR;
	}
    return setModemRegisters(&MODEM_CONFIG_TABLE[index]);
}

HAL_StatusTypeDef setPreambleLength(uint16_t bytes)
{
    if (HAL_OK == spiWrite(RH_RF98_REG_20_PREAMBLE_MSB, bytes >> 8)
    &&	HAL_OK == spiWrite(RH_RF98_REG_21_PREAMBLE_LSB, bytes & 0xFF)
		)
	{
		return HAL_OK;
	}
	return HAL_ERROR;
}

/**
  * @@brief		RF_SetFrequency
  *
  */
HAL_StatusTypeDef RF_SetFrequency(float centre)
{
    // Frf = FRF / FSTEP
    uint32_t frf = (centre * 1000000.0f) / RH_FSTEP;
    if (HAL_OK == spiWrite(RH_RF98_REG_06_FRF_MSB, (frf >> 16) & 0xFF)
    &&	HAL_OK == spiWrite(RH_RF98_REG_07_FRF_MID, (frf >> 8) & 0xFF)
    &&	HAL_OK == spiWrite(RH_RF98_REG_08_FRF_LSB, frf & 0xFF)
		)
	{
		return HAL_OK;
	}
	return HAL_ERROR;
}

/**
  * @@brief		RF_SetTxPower
  * @params
  *		power	from 5 to 23
  */
HAL_StatusTypeDef RF_SetTxPower(int8_t power)
{
	HAL_StatusTypeDef status;

	if (power > 23)	power = 23;
	if (power < 5)	power = 5;

	// For RH_RF98_PA_DAC_ENABLE, manual says '+20dBm on PA_BOOST when OutputPower = 0xF'
	// RH_RF98_PA_DAC_ENABLE actually adds about 3dBm to all power levels. We will us it
	// for 21, 22 and 23dBm
	if (power > 20)
	{
		status = spiWrite(RH_RF98_REG_4D_PA_DAC, RH_RF98_PA_DAC_ENABLE);
		power -= 3;
	}
	else
	{
		status = spiWrite(RH_RF98_REG_4D_PA_DAC, RH_RF98_PA_DAC_DISABLE);
	}

    // RFM95/96/97/98 does not have RFO pins connected to anything. Only PA_BOOST
    // pin is connected, so must use PA_BOOST
    // Pout = 2 + OutputPower.
    // The documentation is pretty confusing on this topic: PaSelect says the max power is 20dBm,
    // but OutputPower claims it would be 17dBm.
    // My measurements show 20dBm is correct
	if (HAL_OK == status
	&&	HAL_OK == spiWrite(RH_RF98_REG_09_PA_CONFIG, RH_RF98_PA_SELECT | (power - 5))
		)
	{
		return HAL_OK;
	}
	return HAL_ERROR;
}

/**
  * @@brief		setModeRx
  *
  */
HAL_StatusTypeDef setModeRx(void)
{
	if (RFContext.Mode != RHModeRx)
	{
		if (HAL_OK == spiWrite(RH_RF98_REG_01_OP_MODE, RH_RF98_MODE_RXCONTINUOUS)
		&&	HAL_OK == spiWrite(RH_RF98_REG_40_DIO_MAPPING1, 0x00)
			)
		{
			// Interrupt on RxDone
			RFContext.Mode = RHModeRx;
		}
		else
		{
			RFContext.Mode = RHModeInitialising;
			return HAL_ERROR;
		}
	}
	return HAL_OK;
}

/**
  * @@brief		setModeTx
  *
  */
HAL_StatusTypeDef setModeTx(void)
{
	if (RFContext.Mode != RHModeTx)
	{
		if (HAL_OK == spiWrite(RH_RF98_REG_01_OP_MODE, RH_RF98_MODE_TX)
		&&	HAL_OK == spiWrite(RH_RF98_REG_40_DIO_MAPPING1, 0x40)
			)
		{
			// Interrupt on TxDone
			RFContext.Mode = RHModeTx;
		}
		else
		{
			RFContext.Mode = RHModeInitialising;
			return HAL_ERROR;
		}
	}
	return HAL_OK;
}

/**
  * @@brief		RF_Init
  *
  */
HAL_StatusTypeDef RF_Init(uint8_t address)
{
	uint16_t timeout = 15;

	RFContext.IrqLevel = 0;
	RFContext.Address = address;
    RFContext.Timeout = RH_DEFAULT_TIMEOUT;
    RFContext.Retries = RH_DEFAULT_RETRIES;
	RFContext.RxReadIndex = RFContext.RxWriteIndex = 0;
	RFContext.TxReadIndex = RFContext.TxWriteIndex = 0;

	// Power On RF module and wait for RESET stay high
	powerOn();
	while (timeout--)
	{
		HAL_Delay(2);
		if (HAL_GPIO_ReadPin(RFRST_PORT, RFRST_PIN) == GPIO_PIN_SET)
			break;
	}

	if (HAL_GPIO_ReadPin(RFRST_PORT, RFRST_PIN) == GPIO_PIN_RESET)
	{
		powerOff();
		return HAL_ERROR;
	}

	HAL_Delay(15);
	
	if (HAL_OK == RF_SPI_On()
	&&	HAL_OK == spiWrite(RH_RF98_REG_01_OP_MODE, RH_RF98_MODE_SLEEP | RH_RF98_LONG_RANGE_MODE)
		)
	{
		uint8_t data;
		HAL_Delay(10); // Wait for sleep mode to take over from say, CAD
		// Check we are in sleep mode, with LORA set
		if (HAL_OK == spiRead(RH_RF98_REG_01_OP_MODE, &data)
		&&	data == (RH_RF98_MODE_SLEEP | RH_RF98_LONG_RANGE_MODE)
			// Set up FIFO
			// We configure so that we can use the entire 256 byte FIFO for either receive
			// or transmit, but not both at the same time
		&&	HAL_OK == spiWrite(RH_RF98_REG_0E_FIFO_TX_BASE_ADDR, 0)
		&&	HAL_OK == spiWrite(RH_RF98_REG_0F_FIFO_RX_BASE_ADDR, 0)
			// Packet format is preamble + explicit-header + payload + crc
			// Explicit Header Mode
			// payload is TO + FROM + ID + FLAGS + message data
			// RX mode is implmented with RXCONTINUOUS
			// max message data length is 255 - 4 = 251 octets
		&&	HAL_OK == RF_SetModeIdle()
			// Set up default configuration
			// No Sync Words in LORA mode.
		&&	HAL_OK == setModemConfig(Bw31_25Cr48Sf512)	// Radio default
		&&	HAL_OK == setPreambleLength(8)				// Default is 8
		&&	HAL_OK == RF_SetFrequency(434.0)
			// Lowish power
		&&	HAL_OK == RF_SetTxPower(13)
			)
		{
			return HAL_OK;
		}
	}
	HAL_SPI_DeInit(&hspi1);
	powerOff();
	return HAL_ERROR;
}

/**
  * @@brief		validateRxBuf
  *				Check whether the latest received message is complete and uncorrupted
  */
bool validateRxBuf(RF_Message_t * msg)
{
	// Extract the 4 headers
	if (RFContext.Promiscuous
	||	msg->Header.To == RFContext.Address
	||	msg->Header.To == RH_BROADCAST_ADDRESS
		)
	{
		RFContext.RxGood++;
		return true;
	}
	return false;
}

/**
  *	@brief	Return slot for RX queue
  *
  */
static RF_Message_t * getRxSlotWrite(void)
{
	uint8_t next = RFContext.RxWriteIndex + 1;
	if (next >= RF_RX_QUEUE_SIZE)
		next = 0;
	if (RFContext.RxReadIndex != next)
	{
		RF_Message_t * msg = &RxQueue[RFContext.RxWriteIndex];
		RFContext.RxWriteIndex = next;
		return msg;
	}
	return NULL;
}

static RF_Message_t * getTxWriteSlot(void)
{
	uint8_t next = RFContext.TxWriteIndex + 1;
	if (next >= RF_TX_QUEUE_SIZE)
		next = 0;
	if (RFContext.TxReadIndex != next)
	{
		RF_Message_t * msg = &TxQueue[RFContext.TxWriteIndex];
		msg->Valid = false;
		RFContext.TxWriteIndex = next;
		return msg;
	}
	return NULL;
}

/**
  * @@brief		RF_FreeRxMessage
  *
  */
void RF_FreeRxMessage(void)
{
	uint8_t next = RFContext.RxReadIndex + 1;
	if (next >= RF_RX_QUEUE_SIZE)
		next = 0;
	RFContext.RxReadIndex = next;
}

static void freeTxMessage(void)
{
	uint8_t next = RFContext.TxReadIndex + 1;
	if (next >= RF_TX_QUEUE_SIZE)
		next = 0;
	RFContext.TxReadIndex = next;
}

/**
  * @@brief		HAL_GPIO_EXTI_Callback
  *
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	// Read the interrupt register
	uint8_t data;
	spiRead(RH_RF98_REG_12_IRQ_FLAGS, &data);
	if (RFContext.Mode == RHModeRx && (data & (RH_RF98_RX_TIMEOUT | RH_RF98_PAYLOAD_CRC_ERROR)))
	{
		RFContext.RxBad++;
	}
	else if (RFContext.Mode == RHModeRx && (data & RH_RF98_RX_DONE) != 0)
	{
		// Have received a packet
		spiRead(RH_RF98_REG_13_RX_NB_BYTES, &data);
		if (data >= RH_HEADER_LEN)
		{
			RF_Message_t * msg = getRxSlotWrite();
			if (msg != NULL)
			{
				msg->Length = data - RH_HEADER_LEN;

				spiRead(RH_RF98_REG_10_FIFO_RX_CURRENT_ADDR, &data);
				spiWrite(RH_RF98_REG_0D_FIFO_ADDR_PTR, data);

				spiBurstRead(RH_RF98_REG_00_FIFO, (uint8_t *)&msg->Header, RH_HEADER_LEN);
				if (RFContext.Promiscuous
				||	msg->Header.To == RFContext.Address
				||	msg->Header.To == RH_BROADCAST_ADDRESS
					)
				{
					RFContext.RxGood++;
					if (msg->Length > 0)
					{
						spiBurstRead(RH_RF98_REG_00_FIFO, (uint8_t *)&msg->Data, msg->Length);
					}

					// Remember the RSSI of this packet
					// this is according to the doc, but is it really correct ?
					// weakest receiveable signals are reported RSSI at about -66
					spiRead(RH_RF98_REG_1A_PKT_RSSI_VALUE, &data);
					msg->Rssi = (data - 137);
				}
				else
				{
					// Discard message from queue
					if (RFContext.RxWriteIndex == 0)
						RFContext.RxWriteIndex = RF_RX_QUEUE_SIZE - 1;
					else
						RFContext.RxWriteIndex--;
				}
			}
			else
			{	// No free RX slots, set Idle mode
				RF_SetModeIdle();
			}
		}
	}
    else if (RFContext.Mode == RHModeTx && (data & RH_RF98_TX_DONE) != 0)
    {
		RFContext.TxGood++;
		RF_SetModeIdle();
    }
    spiWrite(RH_RF98_REG_12_IRQ_FLAGS, 0xFF); // Clear all IRQ flags
}

/**
  * @@brief		RF_RecvFrom
  *
  */
RF_Message_t * RF_RecvFrom(void)
{
	if (RFContext.Mode != RHModeTx
	&&	HAL_OK == setModeRx()
	&&	RFContext.RxReadIndex != RFContext.RxWriteIndex
		)
	{	// Will be set by the interrupt handler when a good message is received
		return &RxQueue[RFContext.RxReadIndex];
	}
	return NULL;
}

static bool waitPacketSent(void)
{
    while (RFContext.Mode == RHModeTx)
	{
		YIELD; // Wait for any previous transmit to finish
	}
    return true;
}

/**
  * @@brief		send
  *
  * @@params
  *		data	data to send
  *		len		data length
  */
static bool send(RF_Message_t * msg)
{
	waitPacketSent(); // Make sure we dont interrupt an outgoing message
	RF_SetModeIdle();

	// Position at the beginning of the FIFO
	if (HAL_OK == spiWrite(RH_RF98_REG_0D_FIFO_ADDR_PTR, 0)
	// The headers and data
	&&	HAL_OK == spiBurstWrite(RH_RF98_REG_00_FIFO, (uint8_t *)&msg->Header, RH_HEADER_LEN)
		)
	{
		if (msg->Data != NULL && msg->Length > 0)
		{	// The message data
			if (HAL_OK != spiBurstWrite(RH_RF98_REG_00_FIFO, (uint8_t *)&msg->Data, msg->Length))
			{
				return false;
			}
		}
		else
		{
			msg->Length = 0;
		}
		if (HAL_OK == spiWrite(RH_RF98_REG_22_PAYLOAD_LENGTH, msg->Length + RH_HEADER_LEN))
		{
			setModeTx();	// Start the transmitter
			// when Tx is done, interruptHandler will fire and radio mode will return to STANDBY
			return true;
		}
	}
	return false;
}

/**
  * @@brief		RF_RecvFromAck
  *
  */
RF_Message_t * RF_RecvFromAck(void)
{
	RF_Message_t * msg = RF_RecvFrom();

	// Get the message before its clobbered by the ACK (shared rx and tx buffer in RH
	if (msg != NULL)	// Never ACK an ACK
	{
		if ((msg->Header.Flags & RH_FLAGS_ACK) != 0)
		{	// ACK package
			if (msg->Header.To == RFContext.Address
			&&	msg->Header.From < RF_MAX_CLIENTS
			&&	RFClients[msg->Header.From].State != RF_CLIENT_INIT
				)
			{

			}
			// RF_FreeRxMessage();
			// return NULL;
			return msg;
		}
		// Its a normal message for this node, not an ACK
		if (msg->Header.To != RH_BROADCAST_ADDRESS)
		{
			/*
			// Its not a broadcast, so ACK it
			// Acknowledge message with ACK set in flags and ID set to received ID
			RF_Message_t * ack = getTxWriteSlot();
			if (ack != NULL)
			{
				ack->Header.To = msg->Header.From;
				ack->Header.From = RFContext.Address;
				ack->Header.Id = msg->Header.Id;
				ack->Header.Flags = msg->Header.Flags;
				ack->Data[0] = '!';
				ack->Length = 1;
				setHeaderFlags(ack, RH_FLAGS_ACK, RH_FLAGS_NONE);
				if (send(ack))
				{
					waitPacketSent();
				}
				freeTxMessage();
			}
			*/
		}
		// If we have not seen this message before, then we are interested in it
		if (msg->Header.Id != seenIds[msg->Header.From])
		{
			seenIds[msg->Header.From] = msg->Header.Id;
			return msg;
		}
		else
		{
			RF_FreeRxMessage();
		}
		// Else just re-ack it and wait for a new one
	}
	// No message for us available
	return NULL;
}

bool RF_SendToEx(void * buf, uint8_t len, uint8_t to, uint8_t from, uint8_t flags)
{
	if (len <= RH_MAX_MESSAGE_LEN)
	{
		RF_Message_t * msg = getTxWriteSlot();
		if (msg != NULL)
		{
			msg->Header.To = to;
			msg->Header.From = from;
			msg->Header.Id = ++RFContext.LastSequenceNumber;
			msg->Header.Flags = flags;
			if (buf != NULL && len > 0)
			{
				memcpy(msg->Data, buf, len);
				msg->Length = len;
			}
			else
			{
				msg->Length = 0;
			}

			if (send(msg))
			{
				freeTxMessage();
				return true;
			}
			freeTxMessage();
		}
	}
	return false;
}

bool RF_SendTo(void * buf, uint8_t len, uint8_t to, uint8_t flags)
{
	return RF_SendToEx(buf, len, to, RFContext.Address, flags);
}

RF_Message_t * RF_RecvFromAckTimeout(uint16_t timeout)
{
	unsigned long starttime = HAL_GetTick();
	while ((HAL_GetTick() - starttime) < timeout)
	{
		RF_Message_t * msg = RF_RecvFromAck();
		if (msg != NULL)
			return msg;
		YIELD;
	}
	return NULL;
}

uint8_t RF_Address(void)
{
	return RFContext.Address;
}

void RF_AddressSet(uint8_t address)
{
	RFContext.Address = address;
}

void RF_SetUniqueID(uint8_t * uid, uint8_t len)
{
	uint8_t * dst = RFContext.UniqueID;
	if (len > RF_UNIQUEID_SIZE)
		len = RF_UNIQUEID_SIZE;
	while (len--)
		*dst++ = *uid++;
}

uint8_t * RF_GetUniqueID(void)
{
	return RFContext.UniqueID;
}

uint8_t RF_FindClient(uint8_t * uid, uint8_t len)
{
	if (len == RF_UNIQUEID_SIZE)
	{
		uint16_t idx;
		// Find client unique id in client table
		for (idx = 1; idx < RF_MAX_CLIENTS; idx++)
		{
			if (RFClients[idx].State != RF_CLIENT_EMPTY
			&&	memcmp(&RFClients[idx].UniqueID, uid, RF_UNIQUEID_SIZE) == 0
				)
			{
				// Found previous slot, return index (as address)
				RFClients[idx].State = RF_CLIENT_INIT;
				return idx;
			}
		}
		// Find free client slot in client table
		for (idx = 1; idx < RF_MAX_CLIENTS; idx++)
		{
			if (RFClients[idx].State == RF_CLIENT_EMPTY)
			{
				// Found free slot, return index (as address)
				RFClients[idx].State = RF_CLIENT_INIT;
				memcpy(&RFClients[idx].UniqueID, uid, RF_UNIQUEID_SIZE);
				return idx;
			}
		}
	}
	return RH_BROADCAST_ADDRESS;
}
