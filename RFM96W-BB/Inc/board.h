#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32l1xx_hal.h"

#define USB_TIMx				TIM3
#define CDC_POLLING_INTERVAL	5

#define USBON_PORT		GPIOA
#define USBON_PIN		GPIO_PIN_8

#define UART_GPIO		GPIOA
#define UART_TXD		GPIO_PIN_9
#define UART_RXD		GPIO_PIN_10

#define LED1_PORT		GPIOB
#define LED1_PIN		GPIO_PIN_11
#define LED2_PORT		GPIOB
#define LED2_PIN		GPIO_PIN_10
#define LED3_PORT		GPIOA
#define LED3_PIN		GPIO_PIN_3

#define RFNSS_PORT		GPIOA
#define RFNSS_PIN		GPIO_PIN_15

#define RFSPI_PORT		GPIOB
#define RFSCK_PIN		GPIO_PIN_3
#define RFMISO_PIN		GPIO_PIN_4
#define RFMOSI_PIN		GPIO_PIN_5

#define RF_DIO0_PORT	GPIOB
#define RF_DIO0_PIN		GPIO_PIN_6
#define RF_DIO0_IRQn	EXTI9_5_IRQn

#define RF_DIO1			GPIO_PIN_7
#define RF_DIO2			GPIO_PIN_8
#define RF_DIO5			GPIO_PIN_12

#define RF_DIO3			GPIO_PIN_2
#define RF_DIO4			GPIO_PIN_1

#define RFRST_PORT		GPIOB
#define RFRST_PIN		GPIO_PIN_15

#define RFPWR_PORT		GPIOB
#define RFPWR_PIN		GPIO_PIN_9

typedef enum ERROR_e {
	NO_ERROR		= 0,
	ERR_RF_INIT		= 0x51,
	ERR_USB_INIT	= 0x52,
	ERR_SYS_FAIL	= 0x53,
} ERROR_t;

void Error_Handler(ERROR_t error);

#endif
