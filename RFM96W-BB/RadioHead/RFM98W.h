#ifndef __RFM98W_H__
#define __RFM98W_H__

#include "board.h"
#include <stdbool.h>

#define RF_SERVER_ADDRESS	0x00

#define RF_UNIQUEID_SIZE	12

// This is the bit in the SPI address that marks it as a write
#define RH_SPI_WRITE_MASK			0x80

// This is the address that indicates a broadcast
#define RH_BROADCAST_ADDRESS		0xFF

// Max number of octets the LORA Rx/Tx FIFO can hold
#define RH_FIFO_SIZE				255

// This is the maximum number of bytes that can be carried by the LORA.
// We use some for headers, keeping fewer for RadioHead messages
#define RH_MAX_PAYLOAD_LEN			RH_FIFO_SIZE

// The length of the headers we add.
// The headers are inside the LORA's payload
#define RH_HEADER_LEN				sizeof(RF_Header_t)

// This is the maximum message length that can be supported by this driver. 
// Can be pre-defined to a smaller size (to save SRAM) prior to including this header
// Here we allow for 1 byte message length, 4 bytes headers, user data and 2 bytes of FCS
#ifndef RH_MAX_MESSAGE_LEN
	#define RH_MAX_MESSAGE_LEN	(RH_MAX_PAYLOAD_LEN - RH_HEADER_LEN)
#endif

typedef __packed struct RF_Header_s {
	uint8_t		To;
	uint8_t		From;
	uint8_t		Id;
	uint8_t		Flags;
} RF_Header_t;

typedef __packed struct RF_Message_s {
	RF_Header_t	Header;
	uint8_t		Data[RH_MAX_MESSAGE_LEN];
	uint8_t		Rssi;
	uint8_t		Length;
	bool		Valid;
} RF_Message_t;

// The acknowledgement bit in the FLAGS
// The top 4 bits of the flags are reserved for RadioHead. The lower 4 bits are reserved
// for application layer use.
#define RH_FLAGS_ACK			0x80

// Defines bits of the FLAGS header reserved for use by the RadioHead library and 
// the flags available for use by applications
#define RH_FLAGS_RESERVED		0xF0
#define RH_FLAGS_APPLICATION	0x0F
#define RH_FLAGS_NONE			0x00
#define RH_FLAGS_DHCP_RQ		0x01
#define RH_FLAGS_DHCP_RSP		0x02
#define RH_FLAGS_BCAST			0x03
#define RH_FLAGS_DATA			0x04


/// the default retry timeout in milliseconds
#define RH_DEFAULT_TIMEOUT			1000

/// The default number of retries
#define RH_DEFAULT_RETRIES			3

// The crystal oscillator frequency of the module
#define RH_FXOSC		32000000.0
// The Frequency Synthesizer step = RH_RF98_FXOSC / 2^^19
#define RH_FSTEP		(RH_FXOSC / 524288)

#define RH_RF98_REG_00_FIFO					0x00
#define RH_RF98_REG_01_OP_MODE				0x01
	#define RH_RF98_MODE					0x07
	#define RH_RF98_MODE_SLEEP				0x00
	#define RH_RF98_MODE_STDBY				0x01
	#define RH_RF98_MODE_FSTX				0x02
	#define RH_RF98_MODE_TX					0x03
	#define RH_RF98_MODE_FSRX				0x04
	#define RH_RF98_MODE_RXCONTINUOUS		0x05
	#define RH_RF98_MODE_RXSINGLE			0x06
	#define RH_RF98_MODE_CAD				0x07
	#define RH_RF98_ACCESS_SHARED_REG		0x40
	#define RH_RF98_LONG_RANGE_MODE			0x80

#define RH_RF98_REG_06_FRF_MSB				0x06
#define RH_RF98_REG_07_FRF_MID				0x07
#define RH_RF98_REG_08_FRF_LSB				0x08
#define RH_RF98_REG_09_PA_CONFIG			0x09
	#define RH_RF98_PA_SELECT				0x80
	#define RH_RF98_OUTPUT_POWER			0x0F

#define RH_RF98_REG_0D_FIFO_ADDR_PTR		0x0D
#define RH_RF98_REG_0E_FIFO_TX_BASE_ADDR	0x0E
#define RH_RF98_REG_0F_FIFO_RX_BASE_ADDR	0x0F
#define RH_RF98_REG_10_FIFO_RX_CURRENT_ADDR	0x10

	#define RH_RF98_RX_TIMEOUT_MASK			0x80
	#define RH_RF98_RX_DONE_MASK			0x40
	#define RH_RF98_PAYLOAD_CRC_ERROR_MASK	0x20
	#define RH_RF98_VALID_HEADER_MASK		0x10
	#define RH_RF98_TX_DONE_MASK			0x08
	#define RH_RF98_CAD_DONE_MASK			0x04
	#define RH_RF98_FHSS_CHANGE_CHANNEL_MASK	0x02
	#define RH_RF98_CAD_DETECTED_MASK			0x01

#define RH_RF98_REG_12_IRQ_FLAGS			0x12
	#define RH_RF98_RX_TIMEOUT				0x80
	#define RH_RF98_RX_DONE					0x40
	#define RH_RF98_PAYLOAD_CRC_ERROR		0x20
	#define RH_RF98_VALID_HEADER			0x10
	#define RH_RF98_TX_DONE					0x08
	#define RH_RF98_CAD_DONE				0x04
	#define RH_RF98_FHSS_CHANGE_CHANNEL		0x02
	#define RH_RF98_CAD_DETECTED			0x01
#define RH_RF98_REG_13_RX_NB_BYTES			0x13
#define RH_RF98_REG_1A_PKT_RSSI_VALUE		0x1A
#define RH_RF98_REG_1D_MODEM_CONFIG1		0x1D
#define RH_RF98_REG_1E_MODEM_CONFIG2		0x1E
#define RH_RF98_REG_20_PREAMBLE_MSB			0x20
#define RH_RF98_REG_21_PREAMBLE_LSB			0x21
#define RH_RF98_REG_22_PAYLOAD_LENGTH		0x22
#define RH_RF98_REG_26_MODEM_CONFIG3		0x26

#define RH_RF98_REG_40_DIO_MAPPING1			0x40
#define RH_RF98_REG_4D_PA_DAC				0x4D
	#define RH_RF98_PA_DAC_DISABLE			0x04
	#define RH_RF98_PA_DAC_ENABLE			0x07

// #define ATOMIC_BLOCK_START	int irq = __disable_irq()
// #define ATOMIC_BLOCK_END	if (irq == 0) __enable_irq()

#define ATOMIC_BLOCK_START		\
	do {						\
		__disable_irq();		\
		RFContext.IrqLevel++;	\
	} while (0)

#define ATOMIC_BLOCK_END		\
	do {						\
		RFContext.IrqLevel--;	\
		if (RFContext.IrqLevel == 0)	\
		{								\
			__enable_irq();				\
		}								\
	} while (0)

#define YIELD

enum {
	RF_CLIENT_EMPTY	= 0,
	RF_CLIENT_INIT	= 1,
};

typedef struct RF_Client_s {
	uint8_t	UniqueID[RF_UNIQUEID_SIZE];
	uint8_t State;
} RF_Client_t;

extern RF_Client_t RFClients[];

typedef enum RHMode_e {
	RHModeInitialising = 0, ///< Transport is initialising. Initial default value until init() is called..
	RHModeSleep,            ///< Transport hardware is in low power sleep mode (if supported)
	RHModeIdle,             ///< Transport is idle.
	RHModeTx,               ///< Transport is in the process of transmitting a message.
	RHModeRx                ///< Transport is in the process of receiving a message.
} RHMode_t;

typedef struct RFContext_s {
			RHMode_t	Mode;
			uint8_t		Address;
			uint32_t	Retransmissions;	/// Count of retransmissions we have had to send
			uint8_t		LastSequenceNumber;	/// The last sequence number to be used. Defaults to 0
			uint16_t	Timeout;	// Retransmit timeout (milliseconds). Defaults to 200
			uint8_t		Retries;	// Retries (0 means one try only). Defaults to 3
			uint8_t		IrqLevel;
						
	volatile uint16_t	RxBad;	/// Count of the number of bad messages (eg bad checksum etc) received
	volatile uint16_t	RxGood;	/// Count of the number of successfully transmitted messaged
	volatile uint16_t	TxGood;	/// Count of the number of bad messages (correct checksum etc) received

			uint8_t		RxReadIndex;
			uint8_t		RxWriteIndex;
			uint8_t		TxReadIndex;
			uint8_t		TxWriteIndex;
			uint8_t		UniqueID[RF_UNIQUEID_SIZE];
						/// Whether the transport is in promiscuous mode
			bool		Promiscuous;
} RFContext_t;

extern RFContext_t RFContext;

uint8_t RF_Address(void);
void RF_AddressSet(uint8_t address);
void RF_SetUniqueID(uint8_t * uid, uint8_t len);
uint8_t * RF_GetUniqueID(void);
uint8_t RF_FindClient(uint8_t * uid, uint8_t len);

HAL_StatusTypeDef RF_Init(uint8_t thisAddress);
RF_Message_t * RF_RecvFromAck(void);
bool RF_SendTo(void * buf, uint8_t len, uint8_t to, uint8_t flags);
bool RF_SendToEx(void * buf, uint8_t len, uint8_t to, uint8_t from, uint8_t flags);
RF_Message_t * RF_RecvFromAckTimeout(uint16_t timeout);
HAL_StatusTypeDef RF_SetFrequency(float centre);
HAL_StatusTypeDef RF_SetTxPower(int8_t power);
void RF_FreeRxMessage(void);

#endif
