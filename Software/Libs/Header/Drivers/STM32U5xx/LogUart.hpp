/**
 ******************************************************************************
 * @file    LogUart.hpp
 * @brief   Print log to the UART interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __LOG_UART_HPP
#define __LOG_UART_HPP

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

namespace Driver
{

/**
 * @brief Debug UART interface.
 */
class LogUart {

public:

    /**
     * @brief Write data to a UART interface.
     * @param data: Data to send to the UART interface.
     * @param len: Data length.
     */
    static void Write(const char *data, size_t len)
    {
        __HAL_RCC_HSI_ENABLE();
        uint32_t cnt = 1000;
        while (READ_BIT(RCC->CR, RCC_CR_HSIRDY) == 0U) {
            if (--cnt == 0) {
                return;
            }
        }
        UART_HandleTypeDef &handle = GetHandle();
        HAL_UART_Transmit(&handle,
                reinterpret_cast<const uint8_t*>(data),
                static_cast<uint16_t>(len), 1000);
    }

    /**
     * @brief Force one-time UART init now, off the fault path.
     *
     * Call once early in boot so the heavy lazy HAL init in GetHandle() never
     * runs from inside an exception (where it could wedge). After this, WriteRaw()
     * has a configured peripheral to write to.
     */
    static void Init()
    {
        (void)GetHandle();
    }

    /**
     * @brief Crash-safe blocking TX straight to the USART registers.
     *
     * No HAL, no HAL_GetTick, no lazy init, no Error_Handler - every wait is a
     * flat bounded spin, so it is safe to call from a fault handler with
     * interrupts masked (SysTick frozen) and cannot hang. Output is best-effort:
     * if the UART is unconfigured or wedged, bytes are dropped rather than
     * blocked. Assumes the peripheral was already configured (see Init()).
     */
    static void WriteRaw(const char *data, size_t len)
    {
        constexpr uint32_t kSpin = 100000;   // bounded; sub-millisecond at any core clock

        __HAL_RCC_HSI_ENABLE();
        for (uint32_t s = kSpin; (READ_BIT(RCC->CR, RCC_CR_HSIRDY) == 0U) && s; --s) {
        }

        USART_TypeDef *u = DEBUG_INSTANCE;
        if ((u->CR1 & USART_CR1_UE) == 0U) {
            return;   // peripheral never configured - nothing to drive
        }

        for (size_t i = 0; i < len; ++i) {
            for (uint32_t s = kSpin; ((u->ISR & USART_ISR_TXE_TXFNF) == 0U) && s; --s) {
            }
            u->TDR = static_cast<uint8_t>(data[i]);   // spin exhausted -> still write, never block
        }

        for (uint32_t s = kSpin; ((u->ISR & USART_ISR_TC) == 0U) && s; --s) {
        }
    }

private:

    /**
     * @brief Initialize UART
     */
    static UART_HandleTypeDef& GetHandle()
    {
        static bool isInit;
        static UART_HandleTypeDef sHuart;

        if (!isInit) {

            // USART3 sources from HSI, which the system clock setup leaves off.
            // Enable it (bounded wait) before the HAL clock/UART config below -
            // otherwise that config has no peripheral clock and hangs. Callers
            // that go through Write() already enable HSI; doing it here too makes
            // GetHandle() self-sufficient for any caller (e.g. Init()).
            __HAL_RCC_HSI_ENABLE();
            for (uint32_t cnt = 1000; (READ_BIT(RCC->CR, RCC_CR_HSIRDY) == 0U) && cnt; --cnt) {
            }

            GPIO_InitTypeDef GPIO_InitStruct = {0};
            RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

            PeriphClkInit.PeriphClockSelection = DEBUG_RCC_PERIPHCLK;
            PeriphClkInit.Usart3ClockSelection = DEBUG_RCC_CLKSOURCE;
            if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
                Error_Handler();
            }

            DEBUG_RCC_CLK_ENABLE_FUNC();

            DEBUG_GPIO_CLK_ENABLE_FUNC();

            GPIO_InitStruct.Pin = DEBUG_TX_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
            GPIO_InitStruct.Alternate = DEBUG_TX_GPIO_ALT;
            HAL_GPIO_Init(DEBUG_TX_GPIO_Port, &GPIO_InitStruct);

            sHuart.Instance = DEBUG_INSTANCE;
            sHuart.Init.BaudRate = 921600;
            sHuart.Init.WordLength = UART_WORDLENGTH_8B;
            sHuart.Init.StopBits = UART_STOPBITS_1;
            sHuart.Init.Parity = UART_PARITY_NONE;
            sHuart.Init.Mode = UART_MODE_TX;
            sHuart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            sHuart.Init.OverSampling = UART_OVERSAMPLING_16;
            sHuart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
            sHuart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
            sHuart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
            if (HAL_UART_Init(&sHuart) != HAL_OK) {
                Error_Handler();
            }

            isInit = true;
        }
        return sHuart;
    }

};

} /* namespace Driver */

#endif /* __LOG_UART_HPP */
