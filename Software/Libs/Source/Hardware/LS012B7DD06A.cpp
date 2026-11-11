/**
 ******************************************************************************
 * @file    LS012B7DD06A.cpp
 * @brief   SHARP %LS012B7DD06A LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "LS012B7DD06A.hpp"

#define LOG_MODULE_PRX      "LS012B7DD06A"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cassert>

#include "Gpio.hpp"
#include "UsTimer.hpp"

#include "stm32u5xx_ll_gpio.h"

// The panel sits a quarter turn round in the case, so a frame arrives in an
// orientation the scan order does not match. Turning it here keeps that fact
// where the panel is described and lets everything above hand over an upright
// frame; a board that mounts the panel straight clears this and drops the
// buffer and the pass with it.
#define PANEL_MOUNTED_ROTATED   1

#if PANEL_MOUNTED_ROTATED

// Both this and the pass below sit above the pragma deliberately: this is
// plain memory work, and the unoptimized build the clocking depends on would
// cost it several milliseconds a frame instead of two.
// Kept to the driver's own dimensions rather than a board-level define, and
// checked against them in write().
static const int32_t skPanelWH = 240;

// Aligned because the line writer reads the frame back a halfword at a time.
alignas(4) static uint8_t sRotationBuffer[skPanelWH * skPanelWH];

static void RotateQuarterTurn(const uint8_t *src, uint8_t *dst)
{
    for (int32_t srcY = 0; srcY < skPanelWH; srcY++) {
        for (int32_t srcX = 0; srcX < skPanelWH; srcX++) {
            dst[(skPanelWH - 1 - srcX) * skPanelWH + srcY] = src[srcY * skPanelWH + srcX];
        }
    }
}

#endif /* PANEL_MOUNTED_ROTATED */

// Don't optimize this file. It must work pricisely as written
#pragma GCC push_options
#pragma GCC optimize ("O0")

namespace Hardware
{

#if 1 // 1 - enable critical section for write
//#include "FreeRTOS.h"
#include "cmsis_os2.h"

#define DISPLAY_GPIO_CRITICAL_START() osKernelLock()
#define DISPLAY_GPIO_CRITICAL_END()   osKernelUnlock()
#else
#define DISPLAY_GPIO_CRITICAL_START()
#define DISPLAY_GPIO_CRITICAL_END()
#endif


// Pins on MCU/NUCLEO-U575ZI-Q
//
// Display (JDI/SHARP)  MCU    NUCLEO-U575ZI-Q
// --------------------------------------------
// 1    VDD2            5V
// 2    VOUT
// 3    VST  / GSP      PB10    CN10.32(CN10.15)
// 4    VCK  / GCK      PA4     CN7.9
// 5    ENB  / GEN      PA7     CN7.14
// 6    XRST / INTB     PB1     CN10.7
// 7    FRP  / VB       PB2     CN9.13
// 8    XFRP / VA       PD14    CN7.16
// 9    VDD1            3V3     CN8.7
// 10   GND
// 11   HST  / BSP      PB0     CN9.7(CN10.21)
// 12   HCK  / BCK      PA6     CN7.12
// 13   R1              PE0     CN10.33
// 14   R2              PE8     CN10.18
// 15   G1              PE2     CN9.14
// 16   G2              PE10    CN10.24
// 17   B1              PE4     CN9.16
// 18   B2              PE12    CN10.23
// 19   HOUT
// 20   VCOM / VCOM
// 21   NC
//
// FRP is the same signal as VCOM, and XFRP is the inverse signal of VCOM and FRP.
//
// Pins are used as follows:
// R1 PE0   R2 PE8
// G1 PE2   G2 PE10
// B1 PE4   B2 PE12

/*
 * Data: PE 0, 2, 4, 8, 10, 12. Only 6 pins are used. Other pins of this port are reserved to make faster port write. Pins should be not connected.
 */

#define DISPLAY_GPIO_HCK_BIT        LCD_QSPI_IO3_HCK_Pin
#define DISPLAY_GPIO_ENB_BIT        LCD_QSPI_IO2_ENB_Pin
#define DISPLAY_GPIO_HST_BIT        LCD_QSPI_IO1_HST_Pin
#define DISPLAY_GPIO_VCK_BIT        LCD_QSPI_CS_VCK_Pin
#define DISPLAY_GPIO_VST_BIT        LCD_QSPI_SCK_VST_Pin
#define DISPLAY_GPIO_RST_BIT        LCD_QSPI_IO0_XRST_Pin

#define DISPLAY_GPIO_HCK_PORT       LCD_QSPI_IO3_HCK_GPIO_Port
#define DISPLAY_GPIO_ENB_PORT       LCD_QSPI_IO2_ENB_GPIO_Port
#define DISPLAY_GPIO_HST_PORT       LCD_QSPI_IO1_HST_GPIO_Port
#define DISPLAY_GPIO_VCK_PORT       LCD_QSPI_CS_VCK_GPIO_Port
#define DISPLAY_GPIO_VST_PORT       LCD_QSPI_SCK_VST_GPIO_Port
#define DISPLAY_GPIO_RST_PORT       LCD_QSPI_IO0_XRST_GPIO_Port

#define DELAY_US(x)     Driver::UsTimer::DelayUs(x)

#define SWITCH_DELAY_US()   do {} while(0)
//#define SWITCH_DELAY_US()   DELAY_US(1)


#define DISPLAY_GPIO_HCK_SET()      do {DISPLAY_GPIO_HCK_PORT->BSRR = DISPLAY_GPIO_HCK_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_ENB_SET()      do {DISPLAY_GPIO_ENB_PORT->BSRR = DISPLAY_GPIO_ENB_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_HST_SET()      do {DISPLAY_GPIO_HST_PORT->BSRR = DISPLAY_GPIO_HST_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_VCK_SET()      do {DISPLAY_GPIO_VCK_PORT->BSRR = DISPLAY_GPIO_VCK_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_VST_SET()      do {DISPLAY_GPIO_VST_PORT->BSRR = DISPLAY_GPIO_VST_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_RST_SET()      do {DISPLAY_GPIO_RST_PORT->BSRR = DISPLAY_GPIO_RST_BIT; SWITCH_DELAY_US();} while(0)

#define DISPLAY_GPIO_HCK_RESET()    do {DISPLAY_GPIO_HCK_PORT->BRR = DISPLAY_GPIO_HCK_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_ENB_RESET()    do {DISPLAY_GPIO_ENB_PORT->BRR = DISPLAY_GPIO_ENB_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_HST_RESET()    do {DISPLAY_GPIO_HST_PORT->BRR = DISPLAY_GPIO_HST_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_VCK_RESET()    do {DISPLAY_GPIO_VCK_PORT->BRR = DISPLAY_GPIO_VCK_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_VST_RESET()    do {DISPLAY_GPIO_VST_PORT->BRR = DISPLAY_GPIO_VST_BIT; SWITCH_DELAY_US();} while(0)
#define DISPLAY_GPIO_RST_RESET()    do {DISPLAY_GPIO_RST_PORT->BRR = DISPLAY_GPIO_RST_BIT; SWITCH_DELAY_US();} while(0)


#define INTB_0	DISPLAY_GPIO_RST_RESET
#define INTB_1	DISPLAY_GPIO_RST_SET

#define GSP_0	DISPLAY_GPIO_VST_RESET
#define GSP_1	DISPLAY_GPIO_VST_SET

#define GCK_0	DISPLAY_GPIO_VCK_RESET
#define GCK_1	DISPLAY_GPIO_VCK_SET

#define GEN_0	DISPLAY_GPIO_ENB_RESET
#define GEN_1	DISPLAY_GPIO_ENB_SET

#define BSP_0	DISPLAY_GPIO_HST_RESET
#define BSP_1	DISPLAY_GPIO_HST_SET

#define BCK_0	DISPLAY_GPIO_HCK_RESET
#define BCK_1	DISPLAY_GPIO_HCK_SET


#define DISPLAY_RGB_MSK             (LCD_R1_Pin | LCD_G1_Pin | LCD_B1_Pin | LCD_R2_Pin | LCD_G2_Pin | LCD_B2_Pin)
#define DISPLAY_RGB_PORT            (LCD_R1_GPIO_Port)

#define DISPLAY_RGB_WRITE(value)    do {LL_GPIO_WriteOutputPort(LCD_R1_GPIO_Port, value); SWITCH_DELAY_US();} while(0)


void LS012B7DD06A::config(Interface::IGpo *vst, Interface::IGpo *vck, Interface::IGpo *enb,
            Interface::IGpo *xrst, Interface::IGpo *hst, Interface::IGpo *hck,
            Interface::IGpo *frp, Interface::IGpo *xfrp,
            Interface::IGpo *r1, Interface::IGpo *r2,
            Interface::IGpo *g1, Interface::IGpo *g2,
            Interface::IGpo *b1, Interface::IGpo *b2)
{
    // This driver is optimized to work directly with MCU peripherals.
    // Therefore, all pins are simply defined for acceleration

    // So just check pins
    assert(vst != nullptr);
    assert(vck != nullptr);
    assert(enb != nullptr);
    assert(xrst != nullptr);
    assert(hst != nullptr);
    assert(hck != nullptr);
    assert(frp != nullptr);
    assert(xfrp != nullptr);
    assert(r1 != nullptr);
    assert(r2 != nullptr);
    assert(g1 != nullptr);
    assert(g2 != nullptr);
    assert(b1 != nullptr);
    assert(b2 != nullptr);
}

bool LS012B7DD06A::init()
{
    if (mIsInit) {
        return true;
    }

    assert(DISPLAY_RGB_PORT == LCD_R1_GPIO_Port);
    assert(DISPLAY_RGB_PORT == LCD_R2_GPIO_Port);
    assert(DISPLAY_RGB_PORT == LCD_G1_GPIO_Port);
    assert(DISPLAY_RGB_PORT == LCD_G2_GPIO_Port);
    assert(DISPLAY_RGB_PORT == LCD_B1_GPIO_Port);
    assert(DISPLAY_RGB_PORT == LCD_B2_GPIO_Port);

    // HAL initialization
    LPTIM_FRP_Init();
    LPTIM_XFRP_Init();

    GPIO_Init();

    Driver::UsTimer::Init();

    mIsInit = true;

    return true;
}

bool LS012B7DD06A::deinit()
{
    if (!mIsInit) {
        return true;
    }

    mIsInit = false;

    GPIO_DeInit();

    LPTIM_FRP_DeInit();
    LPTIM_XFRP_DeInit();

    return true;
}

bool LS012B7DD06A::enable()
{
    if (mIsInit == false) {
        return false;
    }

    if (mIsOn == false) {
        __HAL_LPTIM_RESET_COUNTER(&mLptimFRP);
        __HAL_LPTIM_RESET_COUNTER(&mLptimXFRP);

        // Let's make sure that there are no pull-down on the pins
        LL_GPIO_SetPinPull(LCD_FRP_GPIO_Port, LCD_FRP_Pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinPull(LCD_XFRP_GPIO_Port, LCD_XFRP_Pin, LL_GPIO_PULL_NO);

        if (HAL_LPTIM_PWM_Start(&mLptimFRP, LPTIM_CHANNEL_1) != HAL_OK) {
            LOG_ERROR("HAL_LPTIM_PWM_Start failed\n");
        }

        if (HAL_LPTIM_PWM_Start(&mLptimXFRP, LPTIM_CHANNEL_1) != HAL_OK) {
            LOG_ERROR("HAL_LPTIM_PWM_Start failed\n");
        }

        mIsOn = true;
    }

    return true;
}

bool LS012B7DD06A::disable()
{
    if (mIsInit == false) {
        return false;
    }
    if (mIsOn == true) {
        HAL_LPTIM_PWM_Stop(&mLptimFRP, LPTIM_CHANNEL_1);
        HAL_LPTIM_PWM_Stop(&mLptimXFRP, LPTIM_CHANNEL_1);

        // Set pull-down to prevent current leakage
        LL_GPIO_SetPinPull(LCD_FRP_GPIO_Port, LCD_FRP_Pin, LL_GPIO_PULL_DOWN);
        LL_GPIO_SetPinPull(LCD_XFRP_GPIO_Port, LCD_XFRP_Pin, LL_GPIO_PULL_DOWN);

        mIsOn = false;
    }

    return true;
}

bool LS012B7DD06A::isEnabled()
{
    return mIsOn;
}

void write_xSB(const uint8_t *pLine, int32_t size, bool msb)
{
	// Minimum execution time is tlwGCK (68.89 us)

	uint8_t shift = msb ? 1 : 0;

	// thsGCK (440 ns)
	BSP_1();

	// thsBSP (220 ns)
	BCK_1();	// BCK=1
	DELAY_US(1);    // Minimal delay is required because the first pixels of each line are lost
	// thwBCK (440 ns)
	BCK_0();	// BCK=2

	BSP_0();
	// tlsBSP ( 220 ns)

	uint16_t *data = (uint16_t*) pLine;
	while (size > 0) {

		DISPLAY_RGB_WRITE((*data) >> shift);
		data++;
		// tsRGB (220 ns)
		BCK_1();	// BCK=3...
		// thRGB (220 ns)
		DISPLAY_RGB_WRITE((*data) >> shift);
		data++;
		// tsRGB (220 ns)
		BCK_0();	// BCK=4...122
		// thRGB (220 ns)
		size -= 4;
	}

	// tlwBCK - thRGB (440 - 220 - 220 ns)

	BCK_1();	// BCK=123
	// tlsGEN - thwBCK (0 - 440 = 0 ns)
	GEN_1();

	// thwBCK (440 ns)
	BCK_0();	// Idle


	DELAY_US(10);	// twhGEN (10.64 us)
	GEN_0();
	DELAY_US(5);	// thhGCK (4.4 us)
}

void LS012B7DD06A::write(const uint8_t *pBuf)
{
    if (mIsInit == false) {
        return;
    }

#if PANEL_MOUNTED_ROTATED
    // Here rather than beside the buffer because skWidth/skHeight are private.
    static_assert(skPanelWH == skWidth && skPanelWH == skHeight,
                  "rotation buffer must match the panel");
    // Deliberately before the critical section: the scheduler only has to stop
    // for the clocking, not for the shuffle that precedes it.
    RotateQuarterTurn(pBuf, sRotationBuffer);
    pBuf = sRotationBuffer;
#endif

    DISPLAY_GPIO_CRITICAL_START();

    // Start frame
    INTB_1();
    DELAY_US(11);	// thsINTB (11 us)
    GSP_1();
    DELAY_US(11);	// thsGSP (11 us)
    GCK_1();	// GCK=1
    DELAY_US(69);	// thwGCK (68.89 us)

    // Write line 1
    GCK_0();	// GCK=2
    write_xSB(pBuf, skWidth, true); 	// min tlwGCK (68.89 us)

    GSP_0();
    DELAY_US(11);	// tlsGSP (11 us)

    GCK_1();	// GCK=3
    write_xSB(pBuf, skWidth, false);	// min thwGCK (68.89 us)

    // Write lines 2 .. DISPLAY_HEIGHT
	// Starting from 1 because the first line is already there
	for (uint16_t i = 1; i < skHeight; i++) {
		pBuf += skWidth;	// Next line

		GCK_0();	// GCK=4...
		write_xSB(pBuf, skWidth, true); 	// min tlwGCK (68.89 us)
		GCK_1();	// GCK=5...
		write_xSB(pBuf, skWidth, false);	// min thwGCK (68.89 us)
	}

	GCK_0();	// GCK=482
	DELAY_US(69);	// tlwGCK (68.89 us)
	GCK_1();	// GCK=483
	DELAY_US(69);	// thwGCK (68.89 us)
	GCK_0();	// GCK=Idle
	// thhINTB - thwGCK (68.94 - 68.89 = 0.05 us)
	INTB_0();
	// End frame

	DISPLAY_GPIO_CRITICAL_END();
}


void LS012B7DD06A::GPIO_Init()
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Configure GPIO pin Output Level
    DISPLAY_GPIO_HCK_RESET();
    DISPLAY_GPIO_ENB_RESET();
    DISPLAY_GPIO_HST_RESET();
    DISPLAY_GPIO_VCK_RESET();
    DISPLAY_GPIO_VST_RESET();
    DISPLAY_GPIO_RST_RESET();

    DISPLAY_RGB_WRITE(0);

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Configure GPIO pins
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = DISPLAY_GPIO_HCK_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_HCK_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_GPIO_ENB_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_ENB_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_GPIO_HST_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_HST_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_GPIO_VCK_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_VCK_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_GPIO_VST_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_VST_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_GPIO_RST_BIT;
    HAL_GPIO_Init(DISPLAY_GPIO_RST_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DISPLAY_RGB_MSK;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DISPLAY_RGB_PORT, &GPIO_InitStruct);


    // Configure unused data pins as inputs
    GPIO_InitStruct.Pin = (uint16_t)(~DISPLAY_RGB_MSK);
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DISPLAY_RGB_PORT, &GPIO_InitStruct);

}

void LS012B7DD06A::GPIO_DeInit()
{
    HAL_GPIO_DeInit(DISPLAY_RGB_PORT, DISPLAY_RGB_MSK);

    HAL_GPIO_DeInit(DISPLAY_GPIO_HCK_PORT, DISPLAY_GPIO_HCK_BIT);
    HAL_GPIO_DeInit(DISPLAY_GPIO_ENB_PORT, DISPLAY_GPIO_ENB_BIT);
    HAL_GPIO_DeInit(DISPLAY_GPIO_HST_PORT, DISPLAY_GPIO_HST_BIT);
    HAL_GPIO_DeInit(DISPLAY_GPIO_VCK_PORT, DISPLAY_GPIO_VCK_BIT);
    HAL_GPIO_DeInit(DISPLAY_GPIO_VST_PORT, DISPLAY_GPIO_VST_BIT);
    HAL_GPIO_DeInit(DISPLAY_GPIO_RST_PORT, DISPLAY_GPIO_RST_BIT);
}

uint16_t LS012B7DD06A::GetPeriod()
{
    // Timers run from LSI at 32kHz
    // Frequency should be up to 60Hz
    const uint32_t period = 32000 / 30;
    return period;
}

void LS012B7DD06A::LPTIM_FRP_Init()
{
    // Initializes the peripherals clock
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPTIM1;
    PeriphClkInit.Lptim1ClockSelection   = RCC_LPTIM1CLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        LOG_ERROR("HAL_RCCEx_PeriphCLKConfig failed\n");
        Error_Handler();
    }
    __HAL_RCC_LPTIM1_CLK_ENABLE();

    // Enable LPTIM in stop mode
    __HAL_RCC_LPTIM1_CLKAM_ENABLE();

    LPTIM_OC_ConfigTypeDef sConfig1 = { 0 };

    mLptimFRP.Instance = LPTIM1;

    mLptimFRP.MspInitCallback = MspEmptyCallback;
    mLptimFRP.MspDeInitCallback = MspEmptyCallback;

    mLptimFRP.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
    mLptimFRP.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV1;
    mLptimFRP.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
    mLptimFRP.Init.Period = GetPeriod();
    mLptimFRP.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
    mLptimFRP.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
    mLptimFRP.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
    mLptimFRP.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;
    mLptimFRP.Init.RepetitionCounter = 0;
    if (HAL_LPTIM_Init(&mLptimFRP) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_Init failed\n");
        Error_Handler();
    }
    sConfig1.Pulse = GetPeriod()/2; // Duty 50%
    sConfig1.OCPolarity = LPTIM_OCPOLARITY_HIGH;
    if (HAL_LPTIM_OC_ConfigChannel(&mLptimFRP, &sConfig1, LPTIM_CHANNEL_1) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_OC_ConfigChannel failed\n");
        Error_Handler();
    }

    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // PB2
    GPIO_InitStruct.Pin = LCD_FRP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; //GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_LPTIM1;
    HAL_GPIO_Init(LCD_FRP_GPIO_Port, &GPIO_InitStruct);
}

void LS012B7DD06A::LPTIM_XFRP_Init()
{
    // Initializes the peripherals clock
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPTIM34;
    PeriphClkInit.Lptim34ClockSelection = RCC_LPTIM34CLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        LOG_ERROR("HAL_RCCEx_PeriphCLKConfig failed\n");
        Error_Handler();
    }
    __HAL_RCC_LPTIM3_CLK_ENABLE();

    // Enable LPTIM in stop mode
    __HAL_RCC_LPTIM3_CLKAM_ENABLE();

    LPTIM_OC_ConfigTypeDef sConfig1 = { 0 };
    mLptimXFRP.Instance = LPTIM3;

    mLptimXFRP.MspInitCallback = MspEmptyCallback;
    mLptimXFRP.MspDeInitCallback = MspEmptyCallback;

    mLptimXFRP.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
    mLptimXFRP.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV1;
    mLptimXFRP.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
    mLptimXFRP.Init.Period = GetPeriod();
    mLptimXFRP.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
    mLptimXFRP.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
    mLptimXFRP.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
    mLptimXFRP.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;
    mLptimXFRP.Init.RepetitionCounter = 0;
    if (HAL_LPTIM_Init(&mLptimXFRP) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_Init failed\n");
        Error_Handler();
    }
    sConfig1.Pulse = GetPeriod()/2; // Duty 50%
    sConfig1.OCPolarity = LPTIM_OCPOLARITY_LOW;
    if (HAL_LPTIM_OC_ConfigChannel(&mLptimXFRP, &sConfig1, LPTIM_CHANNEL_1) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_OC_ConfigChannel failed\n");
        Error_Handler();
    }

    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // PD14
    GPIO_InitStruct.Pin = LCD_XFRP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; //GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF14_LPTIM3;
    HAL_GPIO_Init(LCD_XFRP_GPIO_Port, &GPIO_InitStruct);
}

void LS012B7DD06A::LPTIM_FRP_DeInit()
{
    if (HAL_LPTIM_DeInit(&mLptimFRP) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_DeInit failed\n");
        Error_Handler();
    }

    __HAL_RCC_LPTIM1_CLK_DISABLE();
   HAL_GPIO_DeInit(LCD_FRP_GPIO_Port, LCD_FRP_Pin);
}

void LS012B7DD06A::LPTIM_XFRP_DeInit()
{
    if (HAL_LPTIM_DeInit(&mLptimXFRP) != HAL_OK) {
        LOG_ERROR("HAL_LPTIM_DeInit failed\n");
        Error_Handler();
    }

    __HAL_RCC_LPTIM3_CLK_DISABLE();
    HAL_GPIO_DeInit(LCD_XFRP_GPIO_Port, LCD_XFRP_Pin);
}

} /* namespace Drivers */

#pragma GCC pop_options
