#include "stm32l1xx_hal.h"
#include "usb_device.h"

/* USER CODE BEGIN Includes */
#include "board.h"
#include "RFM98W.h"
#include <stdlib.h>
#include "md5.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/*
#include "RHReliableDatagram.h"
#include "RH_RF95.h"

RH_RF95 driver;
RHReliableDatagram manager;
*/
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC_Init(void);

/* USER CODE BEGIN PFP */
const char FW_VERSION[]		= "\r\nRFM98W V1.0\r\n";
const char MSG_RF_FAIL[]	= "RF Module failure\r\n";
const char MSG_RF_OK[]		= "RF Module OK\r\n";
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

void LED1_Off(void)		{	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET);	}
void LED1_On (void)		{	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);	}
void LED2_Off(void)		{	HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, GPIO_PIN_RESET);	}
void LED2_On (void)		{	HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, GPIO_PIN_SET);	}
void LED3_Off(void)		{	HAL_GPIO_WritePin(LED3_PORT, LED3_PIN, GPIO_PIN_RESET);	}
void LED3_On (void)		{	HAL_GPIO_WritePin(LED3_PORT, LED3_PIN, GPIO_PIN_SET);	}

void Error_Handler(ERROR_t error)
{
	while(1)
	{
		LED1_On();
		LED2_On();
		LED3_On();
	}
}

void DebugChar(char ch)
{
	char cx = ch;
	if (cx != '\r' && cx != '\n' && (cx < ' ' || cx > 0x7F))
		cx = '.';
	HAL_UART_Transmit(&huart1, (uint8_t *)&cx, 1, 5);
}

void Debug(const char *msg)
{
	char ch;
	while ((ch = *msg++) != 0)
	{
		DebugChar(ch);
	}
}

void DebugLn(void)
{
	Debug("\r\n");
}

void DebugEx(const char *msg, uint8_t len)
{
	char ch;
	while (len--)
	{
		ch = *msg++;
		DebugChar(ch);
	}
}

void DebugNibble(uint8_t value)
{
	value &= 0x0F;
	if (value >= 0x0A)
	{
		value += ('A' - 0x0A);
	}
	else
	{
		value += '0';
	}
	DebugChar((char)value);
}

void DebugHexU8(uint8_t value)
{
	DebugNibble(value >> 4);
	DebugNibble(value);
}

void DebugDecU8(int8_t value)
{
	bool disp = false;
	int8_t ch;
	int8_t div = 100;
	if (value == 0)
	{
		DebugChar('0');
	}
	else
	if (value < 0)
	{
		DebugChar('-');
		value = -value;
	}
	while (div != 0)
	{
		ch = value / div;
		value %= div;
		if (ch != 0 || disp || div == 1)
		{
			DebugNibble(ch);
			disp = true;
		}
		div /= 10;
	}
}

void DebugHexU16(uint16_t value)
{
	DebugHexU8(value >> 8);
	DebugHexU8(value);
}

void DebugHexU32(uint32_t value)
{
	DebugHexU16(value >> 16);
	DebugHexU16(value);
}

void DebugHex(uint8_t * src, uint8_t len)
{
	while (len--)
	{
		DebugHexU8(*src++);
		DebugChar(' ');
	}
}

#ifdef IS_CLIENT
const uint8_t RF_Package[] = { 0x0D, 0x0E, 0x0A, 0x0D, 0x00, 0x0B, 0x0E, 0x0A, 0x0F };
#endif

#define MCU_UNIQUE_ID_0		*((uint32_t *)0x1FF80050)
#define MCU_UNIQUE_ID_1		*((uint32_t *)0x1FF80054)
#define MCU_UNIQUE_ID_2		*((uint32_t *)0x1FF80064)

/* USER CODE END 0 */

int main(void)
{
	/* USER CODE BEGIN 1 */
	HAL_EnableDBGSleepMode();
	HAL_EnableDBGStopMode();
	HAL_EnableDBGStandbyMode();

	/* USER CODE END 1 */

	/* MCU Configuration----------------------------------------------------------*/
	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* Configure the system clock */
	SystemClock_Config();

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_ADC_Init();
	MX_USART1_UART_Init();
	MX_USB_DEVICE_Init();

	/* USER CODE BEGIN 2 */
	Debug(FW_VERSION);
#ifdef IS_CLIENT
	Debug("CLIENT\r\n");
#else
	Debug("SERVER\r\n");
	LED2_On();
#endif
	/* Prepare for random generator */
	{
		uint32_t value = 0;
		int count;
		if (HAL_ADC_Start(&hadc) == HAL_OK
		&&	HAL_ADC_PollForConversion(&hadc, 5) == HAL_OK
			)
		{
			value = HAL_ADC_GetValue(&hadc);
		}
		else
		{
			HAL_ADC_DeInit(&hadc);
			Error_Handler(ERR_SYS_FAIL);
		}
		HAL_ADC_DeInit(&hadc);

		srand(value);
		count = rand() & 0xF + 2;
		while (count != 0)
		{
			count--;
			value = rand();
			srand(value);
		}
	}

	if (RF_Init(RF_SERVER_ADDRESS) != HAL_OK)
	{
		Debug(MSG_RF_FAIL);
		Error_Handler(ERR_RF_INIT);
	}
	Debug(MSG_RF_OK);

	// RF_SetTxPower(23);
	{
		uint32_t uid[3];

		uid[0] = MCU_UNIQUE_ID_0;
		uid[1] = MCU_UNIQUE_ID_1;
		uid[2] = MCU_UNIQUE_ID_2;
		RF_SetUniqueID((uint8_t *)&uid[0], sizeof(uid));
		Debug("UID : ");
		DebugHex((uint8_t *)&uid[0], sizeof(uid));
		DebugLn();
	}
	/* USER CODE END 2 */

	/* USER CODE BEGIN 3 */

	/* Infinite loop */
	uint32_t startTime = HAL_GetTick();
	while (1)
	{
		RF_Message_t * msg;
		while ((msg = RF_RecvFromAck()) != NULL)
		{
			LED1_On();

			Debug("RECV To:");
			DebugHexU8(msg->Header.To);
			Debug(" From:");
			DebugHexU8(msg->Header.From);
			Debug(" Id:");
			DebugHexU8(msg->Header.Id);
			Debug(" Flags:");
			DebugHexU8(msg->Header.Flags);
			Debug(" Rssi:");
			DebugDecU8(msg->Rssi);
			Debug(" Length:");
			DebugHexU8(msg->Length);
			Debug(" Data:");
			if (msg->Length > 0)
				DebugHex(msg->Data, msg->Length);
			DebugLn();

#ifdef IS_CLIENT
			if (RF_Address() == RF_SERVER_ADDRESS
			&&	msg->Header.To == RH_BROADCAST_ADDRESS
				)
			{
				// Check for broadcast from server
				if (msg->Header.From == RF_SERVER_ADDRESS
				&&	msg->Header.Flags == RH_FLAGS_BCAST
					)
				{
					RF_SendTo(&RFContext.UniqueID, RF_UNIQUEID_SIZE, RF_SERVER_ADDRESS, RH_FLAGS_DHCP_RQ);
				}
				else
				if (msg->Header.Flags == RH_FLAGS_DHCP_RSP
				&&	msg->Length == RF_UNIQUEID_SIZE
				&&	memcmp(msg->Data, RFContext.UniqueID, RF_UNIQUEID_SIZE) == 0
					)
				{
					RF_AddressSet(msg->Header.From);

					Debug("Address:");
					DebugHexU8(RF_Address());
					DebugLn();
				}
			}
#else
			if (msg->Header.To == RF_SERVER_ADDRESS
			&&	msg->Header.From == RF_SERVER_ADDRESS
			&&	msg->Header.Flags == RH_FLAGS_DHCP_RQ
				)
			{
				uint8_t address = RF_FindClient(msg->Data, msg->Length);
				if (address != RH_BROADCAST_ADDRESS)
				{
					RF_SendToEx(msg->Data, msg->Length, RH_BROADCAST_ADDRESS, address, RH_FLAGS_DHCP_RSP);

					Debug("Assign:");
					DebugHexU8(address);
					DebugLn();
				}
			}
#endif
			RF_FreeRxMessage();
			LED1_Off();
		}

#ifdef IS_CLIENT
		if ((HAL_GetTick() - startTime) >= 5000)
		{
			if (RF_Address() != RF_SERVER_ADDRESS)
			{
				LED3_On();
				RF_SendTo((void *)&RF_Package, sizeof(RF_Package), RF_SERVER_ADDRESS, RH_FLAGS_DATA);
				LED3_Off();
			}
			startTime = HAL_GetTick();
		}
#else
		if ((HAL_GetTick() - startTime) >= 15000)
		{
			LED3_On();
			Debug("Send broadcast\r\n");
			RF_SendTo(NULL, 0, RH_BROADCAST_ADDRESS, RH_FLAGS_BCAST);
			startTime = HAL_GetTick();
			LED3_Off();
		}
#endif
	}
	/* USER CODE END 3 */
}

/** System Clock Configuration
*/
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct;
	RCC_ClkInitTypeDef RCC_ClkInitStruct;

	__PWR_CLK_ENABLE();

	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = 16;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
	RCC_OscInitStruct.PLL.PLLDIV = RCC_PLL_DIV3;
	HAL_RCC_OscConfig(&RCC_OscInitStruct);

	RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);

	__SYSCFG_CLK_ENABLE();
}

/* ADC init function */
void MX_ADC_Init(void)
{
	ADC_ChannelConfTypeDef sConfig;

	/**Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion) 
	*/
	hadc.Instance = ADC1;
	hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
	hadc.Init.Resolution = ADC_RESOLUTION12b;
	hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc.Init.ScanConvMode = DISABLE;
	hadc.Init.EOCSelection = EOC_SEQ_CONV;
	hadc.Init.LowPowerAutoWait = ADC_AUTOWAIT_DISABLE;
	hadc.Init.LowPowerAutoPowerOff = ADC_AUTOPOWEROFF_DISABLE;
	hadc.Init.ChannelsBank = ADC_CHANNELS_BANK_A;
	hadc.Init.ContinuousConvMode = ENABLE;
	hadc.Init.NbrOfConversion = 1;
	hadc.Init.DiscontinuousConvMode = DISABLE;
	hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc.Init.DMAContinuousRequests = DISABLE;
	HAL_ADC_Init(&hadc);

	sConfig.SamplingTime = ADC_SAMPLETIME_4CYCLES;

	/*	Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time. */
	sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
	sConfig.Rank = 1;
	HAL_ADC_ConfigChannel(&hadc, &sConfig);
}

/* USART1 init function */
void MX_USART1_UART_Init(void)
{
	huart1.Instance = USART1;
	huart1.Init.BaudRate = 115200;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;
	HAL_UART_Init(&huart1);
}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
        * Free pins are configured automatically as Analog (this feature is enabled through 
        * the Code Generation settings)
*/
void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	/* GPIO Ports Clock Enable */
	__GPIOA_CLK_ENABLE();
	__GPIOB_CLK_ENABLE();
	__GPIOC_CLK_ENABLE();
	__GPIOH_CLK_ENABLE();

	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = GPIO_NOPULL;

	/*Configure GPIO pins : PC13 PC14 PC15 */
	GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/*Configure GPIO pins : PH0 PH1 */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
	HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

	/*Configure GPIO pins : PA0 PA4 PA5 PA6 PA7 */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/*Configure GPIO pins : PB0 PB1 PB2 PB13 PB14 */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/*Configure GPIO pins : PA3 PA8 */
	GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_8;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/*Configure GPIO pins : PB10 PB11 */
	GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/*Configure GPIO pins : PB9 */
	GPIO_InitStruct.Pin = GPIO_PIN_9;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
		ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* USER CODE END 6 */
}

#endif
