#include "stm32l1xx_hal.h"
#include "stm32l1xx.h"
#include "stm32l1xx_it.h"

extern PCD_HandleTypeDef hpcd_USB_FS;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;
extern ADC_HandleTypeDef hadc;

/******************************************************************************/
/*            Cortex-M3 Processor Interruption and Exception Handlers         */ 
/******************************************************************************/

/**
* @brief This function handles USB Low Priority interrupt.
*/
void USB_LP_IRQHandler(void)
{
	HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

/**
* @brief This function handles System tick timer.
*/
void SysTick_Handler(void)
{
	HAL_IncTick();
	HAL_SYSTICK_IRQHandler();
}

/**
* @brief This function handles SPI1 global interrupt.
*/
void SPI1_IRQHandler(void)
{
	HAL_SPI_IRQHandler(&hspi1);
}

/**
* @brief This function handles USART1 global interrupt.
*/
void USART1_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart1);
}

/**
* @brief This function handles EXTI Line[9:5] interrupts.
*/
void EXTI9_5_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}
