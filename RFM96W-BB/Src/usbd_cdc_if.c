#include "usbd_cdc_if.h"

#include "board.h"

/* Define size for the receive and transmit buffer over CDC */
/* It's up to user to redefine and/or remove those define */
#define APP_RX_DATA_SIZE  CDC_DATA_FS_OUT_PACKET_SIZE
#define APP_TX_DATA_SIZE  CDC_DATA_FS_IN_PACKET_SIZE

USBD_CDC_LineCodingTypeDef LineCoding = {
	115200, /* baud rate*/
	0x00,   /* stop bits-1*/
	0x00,   /* parity - none*/
	0x08    /* nb. of bits 8*/
};

/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/* Received Data over USB are stored in this buffer       */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
/* Send Data over USB CDC are stored in this buffer       */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

uint32_t UserTxBufPtrIn = 0;	/*	Increment this pointer or roll it back to
									start address when data are received over USART
								*/
uint32_t UserTxBufPtrOut = 0;	/*	Increment this pointer or roll it back to
									start address when data are sent over USB
								*/
/* USB handler declaration */
/* Handle for USB Full Speed IP */
USBD_HandleTypeDef  *hUsbDevice_0;
extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS     (void);
static int8_t CDC_DeInit_FS   (void);
static int8_t CDC_Control_FS  (uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS  (uint8_t* pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = 
{
	CDC_Init_FS,
	CDC_DeInit_FS,
	CDC_Control_FS,  
	CDC_Receive_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  CDC_Init_FS
  *         Initializes the CDC media low layer over the FS USB IP
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
	hUsbDevice_0 = &hUsbDeviceFS;
	/* USER CODE BEGIN 4 */ 

	/* Set Application Buffers */
	USBD_CDC_SetTxBuffer(hUsbDevice_0, UserTxBufferFS, 0);
	USBD_CDC_SetRxBuffer(hUsbDevice_0, UserRxBufferFS);
	return (USBD_OK);
	/* USER CODE END 4 */ 
}

/**
  * @brief  CDC_DeInit_FS
  *         DeInitializes the CDC media low layer
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
	/* USER CODE BEGIN 5 */ 
	return (USBD_OK);
	/* USER CODE END 5 */ 
}

/**
  * @brief  CDC_Control_FS
  *         Manage the CDC class requests
  * @param  cmd: Command code            
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS  (uint8_t cmd, uint8_t* pbuf, uint16_t length)
{ 
	/* USER CODE BEGIN 6 */
	switch (cmd)
	{
	case CDC_SEND_ENCAPSULATED_COMMAND:
		break;

	case CDC_GET_ENCAPSULATED_RESPONSE:
		break;

	case CDC_SET_COMM_FEATURE:
		break;

	case CDC_GET_COMM_FEATURE:
		break;

	case CDC_CLEAR_COMM_FEATURE:
		break;

	/*******************************************************************************/
	/* Line Coding Structure                                                       */
	/*-----------------------------------------------------------------------------*/
	/* Offset | Field       | Size | Value  | Description                          */
	/* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
	/* 4      | bCharFormat |   1  | Number | Stop bits                            */
	/*                                        0 - 1 Stop bit                       */
	/*                                        1 - 1.5 Stop bits                    */
	/*                                        2 - 2 Stop bits                      */
	/* 5      | bParityType |  1   | Number | Parity                               */
	/*                                        0 - None                             */
	/*                                        1 - Odd                              */ 
	/*                                        2 - Even                             */
	/*                                        3 - Mark                             */
	/*                                        4 - Space                            */
	/* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
	/*******************************************************************************/
	case CDC_SET_LINE_CODING:
		LineCoding.bitrate    = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | (pbuf[2] << 16) | (pbuf[3] << 24));
		LineCoding.format     = pbuf[4];
		LineCoding.paritytype = pbuf[5];
		LineCoding.datatype   = pbuf[6];
		break;

	case CDC_GET_LINE_CODING:     
		pbuf[0] = (uint8_t)(LineCoding.bitrate);
		pbuf[1] = (uint8_t)(LineCoding.bitrate >> 8);
		pbuf[2] = (uint8_t)(LineCoding.bitrate >> 16);
		pbuf[3] = (uint8_t)(LineCoding.bitrate >> 24);
		pbuf[4] = LineCoding.format;
		pbuf[5] = LineCoding.paritytype;
		pbuf[6] = LineCoding.datatype;     
		break;

	case CDC_SET_CONTROL_LINE_STATE:
		break;

	case CDC_SEND_BREAK:
		break;    
    
	default:
		break;
	}
	
	return (USBD_OK);
	/* USER CODE END 6 */
}

/**
  * @brief  CDC_Receive_FS
  *         Data received over USB OUT endpoint are sent over CDC interface 
  *         through this function.
  *           
  *         @note
  *         This function will block any OUT packet reception on USB endpoint 
  *         untill exiting this function. If you exit this function before transfer
  *         is complete on CDC interface (ie. using DMA controller) it will result 
  *         in receiving more data while previous ones are still not sent.
  *                 
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS (uint8_t* Buf, uint32_t *Len)
{
	/* USER CODE BEGIN 7 */ 
	CDC_Transmit_FS(Buf, *Len);
	USBD_CDC_ReceivePacket(hUsbDevice_0);
	return (USBD_OK);
	/* USER CODE END 7 */ 
}

/**
  * @brief  CDC_Transmit_FS
  *         Data send over USB IN endpoint are sent over CDC interface 
  *         through this function.           
  *         @note
  *         
  *                 
  * @param  Buf: Buffer of data to be send
  * @param  Len: Number of data to be send (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
	uint8_t result = USBD_OK;
	/* USER CODE BEGIN 8 */ 
	uint16_t count = Len;
	uint8_t *dst = UserTxBufferFS;
	while (count != 0)
	{
		*dst++ = *Buf++;
		count--;
	}

	USBD_CDC_SetTxBuffer(hUsbDevice_0, UserTxBufferFS, Len);
	result = USBD_CDC_TransmitPacket(hUsbDevice_0);
	/* USER CODE END 8 */ 
	return result;
}
