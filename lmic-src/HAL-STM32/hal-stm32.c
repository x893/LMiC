/*******************************************************************************
 * Copyright (c) 2014 IBM Corporation.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *	IBM Zurich Research Lab - initial API, implementation and documentation
 *******************************************************************************/


#include "lmic.h"
#include "stm32l1xx.h"

// HAL STATE
static struct
{
	int irqlevel;
	uint32_t ticks;
} HAL;

// -----------------------------------------------------------------------------
// GPIOCFG macros
#define GPIOCFG_AF_MASK			0x000F

#define GPIOCFG_MODE_SHIFT		4
#define GPIOCFG_MODE_MASK		(3 << GPIOCFG_MODE_SHIFT)
#define GPIOCFG_MODE_INP		(0 << GPIOCFG_MODE_SHIFT)
#define GPIOCFG_MODE_OUT		(1 << GPIOCFG_MODE_SHIFT)
#define GPIOCFG_MODE_ALT		(2 << GPIOCFG_MODE_SHIFT)
#define GPIOCFG_MODE_ANA		(3 << GPIOCFG_MODE_SHIFT)

#define GPIOCFG_OSPEED_SHIFT	6
#define GPIOCFG_OSPEED_MASK		(3 << GPIOCFG_OSPEED_SHIFT)
#define GPIOCFG_OSPEED_400kHz	(0 << GPIOCFG_OSPEED_SHIFT)
#define GPIOCFG_OSPEED_2MHz		(1 << GPIOCFG_OSPEED_SHIFT)
#define GPIOCFG_OSPEED_10MHz	(2 << GPIOCFG_OSPEED_SHIFT)
#define GPIOCFG_OSPEED_40MHz	(3 << GPIOCFG_OSPEED_SHIFT)

#define GPIOCFG_OTYPE_SHIFT		8
#define GPIOCFG_OTYPE_MASK		(1 << GPIOCFG_OTYPE_SHIFT)
#define GPIOCFG_OTYPE_PUPD		(0 << GPIOCFG_OTYPE_SHIFT)
#define GPIOCFG_OTYPE_OPEN		(1 << GPIOCFG_OTYPE_SHIFT)

#define GPIOCFG_PUPD_SHIFT		9
#define GPIOCFG_PUPD_MASK		(3 << GPIOCFG_PUPD_SHIFT)
#define GPIOCFG_PUPD_NONE		(0 << GPIOCFG_PUPD_SHIFT)
#define GPIOCFG_PUPD_PUP		(1 << GPIOCFG_PUPD_SHIFT)
#define GPIOCFG_PUPD_PDN		(2 << GPIOCFG_PUPD_SHIFT)
#define GPIOCFG_PUPD_RFU		(3 << GPIOCFG_PUPD_SHIFT)

#define GPIO_IRQ_MASK			0x38
#define GPIO_IRQ_FALLING		0x20
#define GPIO_IRQ_RISING			0x28

// GPIO by port number (A=0, B=1, ..)
#define GPIOx(no)				((GPIO_TypeDef*) (GPIOA_BASE + (no) * (GPIOB_BASE - GPIOA_BASE)))

#define GPIO_AF_BITS			4		// width of bit field
#define GPIO_AF_MASK			0x0F	// mask in AFR[0/1]
#define GPIO_AFRLR(i)			((i) >> 3)
#define GPIO_AF_PINi(i,af)		((af) << (((i)&7)*GPIO_AF_BITS))
#define GPIO_AF_set(gpio,i,af)	((gpio)->AFR[GPIO_AFRLR(i)] =			\
									((gpio)->AFR[GPIO_AFRLR(i)]			\
									&  ~GPIO_AF_PINi(i,GPIO_AF_MASK))	\
									|   GPIO_AF_PINi(i,af))

static void hw_cfg_pin (GPIO_TypeDef* gpioport, uint8_t pin, uint16_t gpiocfg)
{
	uint8_t pin2 = pin << 1;

	GPIO_AF_set(gpioport, pin, gpiocfg & GPIOCFG_AF_MASK);
	gpioport->MODER   = (gpioport->MODER   & ~(3 << pin2)) | (((gpiocfg >> GPIOCFG_MODE_SHIFT  ) & 3) << pin2);
	gpioport->OSPEEDR = (gpioport->OSPEEDR & ~(3 << pin2)) | (((gpiocfg >> GPIOCFG_OSPEED_SHIFT) & 3) << pin2);
	gpioport->OTYPER  = (gpioport->OTYPER  & ~(1 << pin )) | (((gpiocfg >> GPIOCFG_OTYPE_SHIFT ) & 1) << pin );
	gpioport->PUPDR   = (gpioport->PUPDR   & ~(3 << pin2)) | (((gpiocfg >> GPIOCFG_PUPD_SHIFT  ) & 3) << pin2);
}

static void hw_cfg_extirq (uint8_t portidx, uint8_t pin, uint8_t irqcfg)
{
	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;	// make sure module is on

	// configure external interrupt (set 4-bit portidx A-G for every pin 0-15)
	uint32_t tmp1 = (pin & 0x3) << 2;
	uint32_t tmp2 = ((uint32_t)0x0F) << tmp1;
	SYSCFG->EXTICR[pin >> 2] = (SYSCFG->EXTICR[pin >> 2] & ~tmp2) | (((uint32_t)portidx) << tmp1);

	// configure trigger and enable irq
	uint32_t mask = (uint32_t)(1 << pin);
	EXTI->RTSR &= ~mask; // clear trigger
	EXTI->FTSR &= ~mask; // clear trigger
	switch(irqcfg & GPIO_IRQ_MASK)
	{
	case GPIO_IRQ_RISING:	// trigger at rising edge
		EXTI->RTSR |= mask;
		break;
	case GPIO_IRQ_FALLING:	// trigger at falling edge
		EXTI->FTSR |= mask;
		break;
	}
	EXTI->IMR  |= mask;  // enable IRQ (pin x for all ports)

	// configure the NVIC
	uint8_t channel = (pin < 5) ? (EXTI0_IRQn + pin) : ((pin < 10) ? EXTI9_5_IRQn : EXTI15_10_IRQn);
	NVIC->IP[channel] = 0x70;	// interrupt priority
	NVIC->ISER[channel >> 5] = 1 << (channel & 0x1F);	// set enable IRQ
}

// -----------------------------------------------------------------------------
// I/O

typedef struct PinInit_s {
	uint8_t		port;
	uint8_t		pin;
	uint8_t		state;
	uint16_t	mode;
} PinInit_t;

#ifdef CFG_sx1276mb1_board

	#define NSS_PORT	1 // NSS: PB6, sx1276
	#define NSS_PIN		6 // sx1276: PB6

	#define SCK_PORT	0 // SCK:  PA5
	#define SCK_PIN		5
	#define MISO_PORT	0 // MISO: PA6
	#define MISO_PIN	6
	#define MOSI_PORT	0 // MOSI: PA7
	#define MOSI_PIN	7

	#define GPIO_AF_SPI1	0x05

	#define TX_PORT		2 // TX:  PC1
	#define TX_PIN		1

	#define RST_PORT	0 // RST: PA0
	#define RST_PIN		0

	#define DIO0_PORT	0	// DIO0: PA10, sx1276   (line 1 irq handler)
	#define DIO0_PIN	10
	#define DIO1_PORT	1	// DIO1: PB3, sx1276  (line 10-15 irq handler)
	#define DIO1_PIN	3
	#define DIO2_PORT	1	// DIO2: PB5, sx1276  (line 10-15 irq handler)
	#define DIO2_PIN	5

	static const uint8_t outputpins[] = { NSS_PORT, NSS_PIN, TX_PORT, TX_PIN  };
	static const uint8_t inputpins[]  = { DIO0_PORT, DIO0_PIN, DIO1_PORT, DIO1_PIN, DIO2_PORT, DIO2_PIN };

#elif CFG_wimod_board

	// output lines
	#define NSS_PORT	1 // NSS: PB0, sx1272
	#define NSS_PIN		0

	#define SCK_PORT	0 // SCK:  PA5
	#define SCK_PIN		5
	#define MISO_PORT	0 // MISO: PA6
	#define MISO_PIN	6
	#define MOSI_PORT	0 // MOSI: PA7
	#define MOSI_PIN	7

	#define GPIO_AF_SPI1	0x05

	#define TX_PORT		0 // TX:  PA4
	#define TX_PIN		4
	#define RX_PORT		2 // RX:  PC13
	#define RX_PIN		13
	#define RST_PORT	0 // RST: PA2
	#define RST_PIN		2

	// input lines
	#define DIO0_PORT	1 // DIO0: PB1   (line 1 irq handler)
	#define DIO0_PIN	1
	#define DIO1_PORT	1 // DIO1: PB10  (line 10-15 irq handler)
	#define DIO1_PIN	10
	#define DIO2_PORT	1 // DIO2: PB11  (line 10-15 irq handler)
	#define DIO2_PIN	11

	static const uint8_t outputpins[] = { NSS_PORT, NSS_PIN, TX_PORT, TX_PIN, RX_PORT, RX_PIN };
	static const uint8_t inputpins[]  = { DIO0_PORT, DIO0_PIN, DIO1_PORT, DIO1_PIN, DIO2_PORT, DIO2_PIN };

#elif CFG_x893_board

	#define NSS_PORT	0	// NSS: PA15, RFM98W
	#define NSS_PIN		15

	#define SCK_PORT	1	// SCK:  PB3
	#define SCK_PIN		3
	#define MISO_PORT	1	// MISO: PB4
	#define MISO_PIN	4
	#define MOSI_PORT	1	// MOSI: PB5
	#define MOSI_PIN	5

	#define GPIO_AF5_SPI1	((uint8_t)0x05)  /* SPI1/I2S1 Alternate Function mapping */
	#define GPIO_AF_SPI1	GPIO_AF5_SPI1

	#define RST_PORT	1	// RST: PB15
	#define RST_PIN		15

	#define PWR_PORT	1	// PWR: PB9
	#define PWR_PIN		9

	// input lines
	#define DIO0_PORT	1 // DIO0: PB6 (line 5-9 irq handler)
	#define DIO0_PIN	6
	#define DIO1_PORT	1 // DIO1: PB7 (line 5-9 irq handler)
	#define DIO1_PIN	7
	#define DIO2_PORT	1 // DIO2: PB8 (line 5-9 irq handler)
	#define DIO2_PIN	8

	static const PinInit_t outputpins[] = {
		{	PWR_PORT,	PWR_PIN,	1,	GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD	},
		{	NSS_PORT,	NSS_PIN,	1,	GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD	},
		{	RST_PORT,	RST_PIN,	1,	GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz | GPIOCFG_PUPD_PUP		}
	};
	static const PinInit_t inputpins[]  = {
		{	DIO0_PORT,	DIO0_PIN,	1,	GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz | GPIOCFG_PUPD_PDN	},
		{	DIO1_PORT,	DIO1_PIN,	1,	GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz | GPIOCFG_PUPD_PDN	},
		{	DIO2_PORT,	DIO2_PIN,	1,	GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz | GPIOCFG_PUPD_PDN	}
	};

#else
	#error Missing CFG_sx1276mb1_board/CFG_wimod_board!
#endif

static void setpin (GPIO_TypeDef * gpioport, uint8_t pin, uint8_t state)
{
	gpioport->ODR = (gpioport->ODR & ~(1 << pin)) | ((state & 1) << pin );
}

#define n_elements(x)	(sizeof(x) / sizeof(*x))

static void hal_io_init ()
{
	uint8_t i;
	// clock enable for GPIO ports A,B,C
	RCC->AHBENR  |= (RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN);

	// configure output lines and set to low state
	for (i = 0; i < n_elements(outputpins); i++)
	{
		if (outputpins[i].state < 2)
		{
			setpin(GPIOx(outputpins[i].port), outputpins[i].pin, outputpins[i].state);
		}
		hw_cfg_pin(GPIOx(outputpins[i].port), outputpins[i].pin, outputpins[i].mode);
	}

	// configure input lines and register IRQ
	for (i = 0; i < n_elements(inputpins); i++)
	{
		hw_cfg_pin(GPIOx(inputpins[i].port), inputpins[i].pin, inputpins[i].mode);
		if (inputpins[i].state != 0)
		{
			hw_cfg_extirq(inputpins[i].port, inputpins[i].pin, GPIO_IRQ_RISING);
		}
	}
}

// val ==1  => tx 1, rx 0 ; val == 0 => tx 0, rx 1
void hal_pin_rxtx (uint8_t val)
{
#ifdef CFG_x893_board

#else
	ASSERT(val == 1 || val == 0);
	#ifndef CFG_sx1276mb1_board
	setpin(GPIOx(RX_PORT), RX_PIN, ~val);
	#endif
	setpin(GPIOx(TX_PORT), TX_PIN, val);
#endif
}

// set radio NSS pin to given value
void hal_pin_nss (uint8_t val)
{
	setpin(GPIOx(NSS_PORT), NSS_PIN, val);
}

// set radio PWR pin to given value
void hal_pin_pwr (uint8_t val)
{
	setpin(GPIOx(PWR_PORT), PWR_PIN, val);
}

// set radio RST pin to given value (or keep floating!)
void hal_pin_rst (uint8_t val)
{
	if (val == 0 || val == 1)
	{	// drive pin
		setpin(GPIOx(RST_PORT), RST_PIN, val);
		hw_cfg_pin(GPIOx(RST_PORT), RST_PIN, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD);
	}
	else
	{	// keep pin floating
		hw_cfg_pin(GPIOx(RST_PORT), RST_PIN, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
	}
}

extern void radio_irq_handler(uint8_t dio);

// generic EXTI IRQ handler for all channels
static void EXTI_IRQHandler ()
{
	// DIO 0
	if ((EXTI->PR & (1 << DIO0_PIN)) != 0)
	{	// pending
		EXTI->PR = (1 << DIO0_PIN); // clear irq
		radio_irq_handler(0);		// invoke radio handler (on IRQ!)
	}
	// DIO 1
	if ((EXTI->PR & (1 << DIO1_PIN)) != 0)
	{	// pending
		EXTI->PR = (1 << DIO1_PIN); // clear irq
		radio_irq_handler(1);		// invoke radio handler (on IRQ!)
	}
	// DIO 2
	if ((EXTI->PR & (1 << DIO2_PIN)) != 0)
	{	// pending
		EXTI->PR = (1 << DIO2_PIN); // clear irq
		radio_irq_handler(2);		// invoke radio handler (on IRQ!)
	}
#ifdef CFG_EXTI_IRQ_HANDLER
	// invoke user-defined interrupt handler
	{
		extern void CFG_EXTI_IRQ_HANDLER();
		CFG_EXTI_IRQ_HANDLER();
	}
#endif // CFG_EXTI_IRQ_HANDLER
}

void EXTI0_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI1_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI2_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI3_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI4_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI9_5_IRQHandler ()
{
	EXTI_IRQHandler();
}

void EXTI15_10_IRQHandler ()
{
	EXTI_IRQHandler();
}

// -----------------------------------------------------------------------------
// SPI

static void hal_spi_init ()
{
	// enable clock for SPI interface 1
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

	// use alternate function SPI1 (SCK, MISO, MOSI)
	hw_cfg_pin(GPIOx(SCK_PORT),  SCK_PIN,  GPIOCFG_MODE_ALT | GPIOCFG_OSPEED_40MHz | GPIO_AF_SPI1 | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
	hw_cfg_pin(GPIOx(MISO_PORT), MISO_PIN, GPIOCFG_MODE_ALT | GPIOCFG_OSPEED_40MHz | GPIO_AF_SPI1 | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
	hw_cfg_pin(GPIOx(MOSI_PORT), MOSI_PIN, GPIOCFG_MODE_ALT | GPIOCFG_OSPEED_40MHz | GPIO_AF_SPI1 | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PDN);
	
	// configure and activate the SPI (master, internal slave select, software slave mgmt)
	// (use default mode: 8-bit, 2-wire, no crc, MSBF, PCLK/2, CPOL0, CPHA0)
	SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_SPE | (SPI_CR1_BR_1);
}

// perform SPI transaction with radio
uint8_t hal_spi (uint8_t out)
{
	SPI1->DR = out;
	while ( (SPI1->SR & SPI_SR_RXNE ) == 0);
	return SPI1->DR; // in
}

// -----------------------------------------------------------------------------
// TIME
static void hal_time_init ()
{
	volatile uint32_t timeout;
	PWR->CR |= PWR_CR_DBP;		// disable write protect

	RCC->CSR |= RCC_CSR_LSEON;	// switch on low-speed oscillator @32.768kHz
	timeout = 100000;
	while (timeout != 0 && (RCC->CSR & RCC_CSR_LSERDY) == 0 )
	{
		timeout--;
	}	// wait for it...
	if ((RCC->CSR & RCC_CSR_LSERDY) == 0)
	{	// Fail with LSE, try LSI
		RCC->CSR &= ~RCC_CSR_LSEON;
		RCC->CSR |= RCC_CSR_LSION;
		timeout = 100000;
		while (timeout != 0 && (RCC->CSR & RCC_CSR_LSIRDY) == 0 )
		{
			timeout--;
		}	// wait for it...
	}

	RCC->APB2ENR   |= RCC_APB2ENR_TIM9EN;		// enable clock to TIM9 peripheral 
	RCC->APB2LPENR |= RCC_APB2LPENR_TIM9LPEN;	// enable clock to TIM9 peripheral also in low power mode
	RCC->APB2RSTR  |= RCC_APB2RSTR_TIM9RST;		// reset TIM9 interface
	RCC->APB2RSTR  &= ~RCC_APB2RSTR_TIM9RST;	// reset TIM9 interface

	if ((RCC->CSR & RCC_CSR_LSERDY) != 0 )
		TIM9->SMCR = TIM_SMCR_ECE;	// external clock enable (source clock mode 2) with no prescaler and no filter
	else
		TIM9->PSC = 900;

	NVIC->IP[TIM9_IRQn] = 0x70;	// interrupt priority
	NVIC->ISER[TIM9_IRQn >> 5] = 1<<(TIM9_IRQn & 0x1F);	// set enable IRQ

	// enable update (overflow) interrupt
	TIM9->DIER |= TIM_DIER_UIE;

	// Enable timer counting
	TIM9->CR1 = TIM_CR1_CEN;
}

uint32_t hal_ticks ()
{
	hal_disableIRQs();
	uint32_t t = HAL.ticks;
	uint16_t cnt = TIM9->CNT;
	if ( (TIM9->SR & TIM_SR_UIF) )
	{
		// Overflow before we read CNT?
		// Include overflow in evaluation but
		// leave update of state to ISR once interrupts enabled again
		cnt = TIM9->CNT;
		t++;
	}
	hal_enableIRQs();
	return (t << 16) | cnt;
}

// return modified delta ticks from now to specified ticktime (0 for past, FFFF for far future)
static uint16_t deltaticks (uint32_t time)
{
	uint32_t t = hal_ticks();
	int32_t d = time - t;
	if ( d <= 0 )
		return 0;	// in the past
	if ((d >> 16) != 0)
		return 0xFFFF; // far ahead
	return (uint16_t)d;
}

void hal_waitUntil (uint32_t time)
{
	while ( deltaticks(time) != 0 ); // busy wait until timestamp is reached
}

// check and rewind for target time
uint8_t hal_checkTimer (uint32_t time)
{
	uint16_t dt;
	TIM9->SR &= ~TIM_SR_CC2IF;	// clear any pending interrupts
	if ((dt = deltaticks(time)) < 5)
	{	// event is now (a few ticks ahead)
		TIM9->DIER &= ~TIM_DIER_CC2IE; // disable IE
		return 1;
	}
	else
	{	// rewind timer (fully or to exact time))
		TIM9->CCR2 = TIM9->CNT + dt;   // set comparator
		TIM9->DIER |= TIM_DIER_CC2IE;  // enable IE
		TIM9->CCER |= TIM_CCER_CC2E;   // enable capture/compare uint 2
		return 0;
	}
}
  
void TIM9_IRQHandler ()
{
	if (TIM9->SR & TIM_SR_UIF)
	{	// overflow
		HAL.ticks++;
	}
	if ((TIM9->SR & TIM_SR_CC2IF) && (TIM9->DIER & TIM_DIER_CC2IE))
	{	// expired
		// do nothing, only wake up cpu
	}
	TIM9->SR = 0; // clear IRQ flags
}

// -----------------------------------------------------------------------------
// IRQ
void hal_disableIRQs ()
{
	__disable_irq();
	HAL.irqlevel++;
}

void hal_enableIRQs ()
{
	--HAL.irqlevel;
	if (HAL.irqlevel == 0)
	{
		__enable_irq();
	}
}

void hal_sleep ()
{
	// low power sleep mode (if LSE enable)
	if ((RCC->CSR & RCC_CSR_LSERDY) != 0)
		PWR->CR |= PWR_CR_LPSDSR;
	// suspend execution until IRQ, regardless of the CPSR I-bit
	__WFI();
}

// -----------------------------------------------------------------------------

void hal_init ()
{
	memset(&HAL, 0x00, sizeof(HAL));
	hal_disableIRQs();

	DBGMCU->CR |= (DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY);
	DBGMCU->APB2FZ |= DBGMCU_APB2_FZ_DBG_TIM9_STOP;

	hal_io_init();		// configure radio I/O and interrupt handler
	hal_spi_init();		// configure radio SPI
	hal_time_init();	// configure timer and interrupt handler

	DEBUG_INIT();

	hal_enableIRQs();
}

void hal_failed ()
{
	// HALT...
	hal_disableIRQs();
	hal_sleep();
	while (1)
	{ ; }
}

//////////////////////////////////////////////////////////////////////
// DEBUG CODE BELOW (use CFG_DEBUG)
//////////////////////////////////////////////////////////////////////
#ifdef CFG_DEBUG
	#if CFG_x893_board
		#define LED_PORT		GPIOB // use GPIO PB11 (LED1 on X893)
		#define LED_PIN			11
	#else
		#define LED_PORT		GPIOA // use GPIO PA8 (LED4 on IMST, P11/PPS/EXT1_10/GPS6 on Blipper)
		#define LED_PIN			8
	#endif

	#define USART_TX_PORT	GPIOA
	#define USART_TX_PIN	9
	#define USART_RX_PORT	GPIOA
	#define USART_RX_PIN	10
	#define GPIO_AF_USART1	0x07

	void debug_init ()
	{
		// configure LED pin as output
		hw_cfg_pin(LED_PORT, LED_PIN, GPIOCFG_MODE_OUT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PUP);
		debug_led(0);

		// configure USART1 (115200/8N1, tx-only)
		RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
		hw_cfg_pin(USART_TX_PORT, USART_TX_PIN, GPIOCFG_MODE_ALT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PUP | GPIO_AF_USART1);
		hw_cfg_pin(USART_RX_PORT, USART_RX_PIN, GPIOCFG_MODE_ALT | GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_PUP | GPIO_AF_USART1);

		USART1->BRR = 277;	// 115200
		USART1->CR1 = USART_CR1_UE | USART_CR1_TE;	// usart + transmitter enable

		// print banner
		debug_str("\r\n============== DEBUG STARTED ==============\r\n");
	}

	void debug_led (uint8_t val)
	{
		setpin(LED_PORT, LED_PIN, val);
	}

	void debug_char (uint8_t c)
	{
		if (c == '\n')
			debug_char ('\r');
		while ( !(USART1->SR & USART_SR_TXE) );
		USART1->DR = c;
	}

	static const char debug_hex_string[] = "0123456789ABCDEF";
	void debug_hex (uint8_t b)
	{
		debug_char(debug_hex_string[b >>  4]);
		debug_char(debug_hex_string[b & 0xF]);
	}

	void debug_buf (const uint8_t * buf, uint16_t len)
	{
		while (len--)
		{
			debug_hex(*buf++);
			debug_char(' ');
		}
		debug_char('\n');
	}

	void debug_uint (uint32_t v)
	{
		for(int n = 24; n >= 0; n -= 8)
		{
			debug_hex(v >> n);
		}
	}

	void debug_str (const char * str)
	{
		while (*str)
		{
			debug_char(*str++);
		}
	}

	void debug_val (const char * label, uint32_t val)
	{
		debug_str(label);
		debug_uint(val);
		debug_char('\n');
	}

	void debug_event (int ev)
	{
		static const char * evnames[] = {
			[EV_SCAN_TIMEOUT]	= "SCAN_TIMEOUT",
			[EV_BEACON_FOUND]	= "BEACON_FOUND",
			[EV_BEACON_MISSED]	= "BEACON_MISSED",
			[EV_BEACON_TRACKED]	= "BEACON_TRACKED",
			[EV_JOINING]		= "JOINING",
			[EV_JOINED]			= "JOINED",
			[EV_RFU1]			= "RFU1",
			[EV_JOIN_FAILED]	= "JOIN_FAILED",
			[EV_REJOIN_FAILED]	= "REJOIN_FAILED",
			[EV_TXCOMPLETE]		= "TXCOMPLETE",
			[EV_LOST_TSYNC]		= "LOST_TSYNC",
			[EV_RESET]			= "RESET",
			[EV_RXCOMPLETE]		= "RXCOMPLETE",
			[EV_LINK_DEAD]		= "LINK_DEAD",
			[EV_LINK_ALIVE]		= "LINK_ALIVE",
		};
		debug_str(evnames[ev]);
		debug_char('\r');
		debug_char('\n');
	}
#endif // CFG_DEBUG

void prvGetRegistersFromStack( uint32_t *pulFaultStackAddress )
{
/*	These are volatile to try and prevent the compiler/linker optimising them
	away as the variables never actually get used.  If the debugger won't show the
	values of the variables, make them global my moving their declaration outside
	of this function.
*/
	volatile uint32_t r0;
	volatile uint32_t r1;
	volatile uint32_t r2;
	volatile uint32_t r3;
	volatile uint32_t r12;
	volatile uint32_t lr; /* Link register. */
	volatile uint32_t pc; /* Program counter. */
	volatile uint32_t psr;/* Program status register. */

	r0 = pulFaultStackAddress[ 0 ];
	r1 = pulFaultStackAddress[ 1 ];
	r2 = pulFaultStackAddress[ 2 ];
	r3 = pulFaultStackAddress[ 3 ];

	r12 = pulFaultStackAddress[ 4 ];
	lr  = pulFaultStackAddress[ 5 ];
	pc  = pulFaultStackAddress[ 6 ];
	psr = pulFaultStackAddress[ 7 ];

	debug_str("\n\n[Hard fault handler - all numbers in hex]\n");
	debug_val("R0  = ", r0);
	debug_val("R1  = ", r1);
	debug_val("R2  = ", r2);
	debug_val("R3  = ", r3);
	debug_val("R12 = ", r12);
	debug_val("LR [R14] = ", lr);	// %x  subroutine call return address\n
	debug_val("PC [R15] = ", pc);	// %x  program counter\n
	debug_val("PSR  = ", psr);
	debug_val("BFAR = ", (*((volatile unsigned long *)(0xE000ED38))));
	debug_val("CFSR = ", (*((volatile unsigned long *)(0xE000ED28))));
	debug_val("HFSR = ", (*((volatile unsigned long *)(0xE000ED2C))));
	debug_val("DFSR = ", (*((volatile unsigned long *)(0xE000ED30))));
	debug_val("AFSR = ", (*((volatile unsigned long *)(0xE000ED3C))));
	debug_val("SCB_SHCSR = ", SCB->SHCSR);

    /* When the following line is hit, the variables contain the register values. */
    for( ;; );
}
