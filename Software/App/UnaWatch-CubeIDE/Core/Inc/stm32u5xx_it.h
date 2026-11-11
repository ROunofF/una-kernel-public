/**
 ******************************************************************************
 * @file    stm32u5xx_it.h
 * @brief   This file contains the headers of the interrupt handlers.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __STM32U5xx_IT_H
#define __STM32U5xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

void TIM17_IRQHandler(void);

void I2C1_EV_IRQHandler(void);
void I2C1_ER_IRQHandler(void);
void I2C2_EV_IRQHandler(void);
void I2C2_ER_IRQHandler(void);
void I2C3_EV_IRQHandler(void);
void I2C3_ER_IRQHandler(void);
void I2C4_EV_IRQHandler(void);
void I2C4_ER_IRQHandler(void);

void OTG_HS_IRQHandler(void);

void SDMMC1_IRQHandler(void);

void RTC_IRQHandler(void);

void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void EXTI5_IRQHandler(void);
void EXTI6_IRQHandler(void);
void EXTI7_IRQHandler(void);
void EXTI8_IRQHandler(void);
void EXTI9_IRQHandler(void);
void EXTI10_IRQHandler(void);
void EXTI11_IRQHandler(void);
void EXTI12_IRQHandler(void);
void EXTI13_IRQHandler(void);
void EXTI14_IRQHandler(void);
void EXTI15_IRQHandler(void);

void SPI1_IRQHandler(void);
void SPI2_IRQHandler(void);
void SPI3_IRQHandler(void);

void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void LPUART1_IRQHandler(void);
void TIM6_IRQHandler(void);

void GPDMA1_Channel0_IRQHandler(void);
void GPDMA1_Channel1_IRQHandler(void);
void GPDMA1_Channel2_IRQHandler(void);
void GPDMA1_Channel3_IRQHandler(void);
void GPDMA1_Channel4_IRQHandler(void);
void GPDMA1_Channel5_IRQHandler(void);
void GPDMA1_Channel6_IRQHandler(void);
void GPDMA1_Channel7_IRQHandler(void);
void GPDMA1_Channel8_IRQHandler(void);
void GPDMA1_Channel9_IRQHandler(void);
void GPDMA1_Channel10_IRQHandler(void);
void GPDMA1_Channel11_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32U5xx_IT_H */
