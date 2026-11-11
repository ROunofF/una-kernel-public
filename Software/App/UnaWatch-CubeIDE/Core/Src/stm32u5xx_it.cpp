/**
 ******************************************************************************
 * @file    stm32u5xx_it.cpp
 * @brief   Interrupt Service Routines.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "main.h"
#include "stm32u5xx_it.h"

#define LOG_MODULE_PRX      "stm32u5xx_it"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Rtc.hpp"
#include "I2c.hpp"
#include "Spi.hpp"
#include "Gpio.hpp"
#include "Mmc.hpp"
#include "Usb.h"
#include "Uart.hpp"
#include "LpUart.hpp"
#include "Crash/CrashDump.h"

// HAL tick timer
//extern TIM_HandleTypeDef htim17;

/**
 * @brief This function handles Non maskable interrupt.
 */
void NMI_Handler(void)
{
    LOG_ERROR("NMI\n");
    Error_Handler();
}

/**
 * @brief This function handles Hard fault interrupt.
 */
// Every handler below starts by clearing MSPLIM, and it has to be before the
// push. When a stack-overflow fault arrives, SP is sitting AT the limit: the
// hardware is allowed to stack the exception frame into the padding below it,
// which is why the padding exists, but the handler's own `push {r4-r11}` is an
// ordinary store and violates the limit again. A second violation while
// already in HardFault is not another fault, it is LOCKUP - no handler runs,
// nothing is recorded, and only the watchdog recovers the device.
//
// Measured, not theorised: `crash msp` produced exactly that - a watchdog
// reset with a synthesized capsule, every register zero, before this was
// added. Clearing the limit costs two instructions and is safe here because
// every one of these paths ends in a reset.

extern "C" __attribute__((naked)) void HardFault_Handler(void)
{
    // r0 = faulting SP, r1 = EXC_RETURN, r2 = &{r4..r11} pushed on MSP.
    // r4..r11 still hold the faulting context here (exception entry does not
    // touch them); push them before any C runs so the handler can record them.
    // r3 is free to clobber: exception entry already stacked r0-r3.
    __asm volatile(
        // Widen MSPLIM first - see the note above HardFault_Handler.
        "mov   r3, #0                     \n"
        "msr   MSPLIM, r3                 \n"
        "tst   lr, #4                     \n"
        "ite   eq                         \n"
        "mrseq r0, msp                    \n"
        "mrsne r0, psp                    \n"
        "mov   r1, lr                     \n"
        "push  {r4-r11}                   \n"
        "mov   r2, sp                     \n"
        "b     CrashDump_handlerHardFault \n"
    );
}

extern "C" __attribute__((naked)) void IWDG_IRQHandler(void)
{
    __asm volatile(
        // Widen MSPLIM first - see the note above HardFault_Handler.
        "mov   r3, #0                     \n"
        "msr   MSPLIM, r3                 \n"
        "tst   lr, #4                     \n"
        "ite   eq                         \n"
        "mrseq r0, msp                    \n"
        "mrsne r0, psp                    \n"
        "mov   r1, lr                     \n"
        "push  {r4-r11}                   \n"
        "mov   r2, sp                     \n"
        "b     CrashDump_handlerWatchDog  \n"
    );
}

// Configurable faults (MemManage / BusFault / UsageFault) are disabled by
// default (SHCSR.*FAULTENA == 0) and escalate to HardFault, which captures the
// dump. If an integrator ever enables one (LL_HANDLER_EnableFault), the fault is
// taken here directly - so route it through the same capture path (identical
// naked prologue) instead of the dump-less Error_Handler. The faulting SP,
// EXC_RETURN and r4-r11 are recorded as for HardFault; CFSR identifies the cause.

extern "C" __attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile(
        // Widen MSPLIM first - see the note above HardFault_Handler.
        "mov   r3, #0                     \n"
        "msr   MSPLIM, r3                 \n"
        "tst   lr, #4                     \n"
        "ite   eq                         \n"
        "mrseq r0, msp                    \n"
        "mrsne r0, psp                    \n"
        "mov   r1, lr                     \n"
        "push  {r4-r11}                   \n"
        "mov   r2, sp                     \n"
        "b     CrashDump_handlerHardFault \n"
    );
}

extern "C" __attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile(
        // Widen MSPLIM first - see the note above HardFault_Handler.
        "mov   r3, #0                     \n"
        "msr   MSPLIM, r3                 \n"
        "tst   lr, #4                     \n"
        "ite   eq                         \n"
        "mrseq r0, msp                    \n"
        "mrsne r0, psp                    \n"
        "mov   r1, lr                     \n"
        "push  {r4-r11}                   \n"
        "mov   r2, sp                     \n"
        "b     CrashDump_handlerHardFault \n"
    );
}

extern "C" __attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile(
        // Widen MSPLIM first - see the note above HardFault_Handler.
        "mov   r3, #0                     \n"
        "msr   MSPLIM, r3                 \n"
        "tst   lr, #4                     \n"
        "ite   eq                         \n"
        "mrseq r0, msp                    \n"
        "mrsne r0, psp                    \n"
        "mov   r1, lr                     \n"
        "push  {r4-r11}                   \n"
        "mov   r2, sp                     \n"
        "b     CrashDump_handlerHardFault \n"
    );
}

/**
 * @brief This function handles Debug monitor.
 */
void DebugMon_Handler(void)
{
}

/******************************************************************************/
/* STM32U5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32u5xx.s).                    */
/******************************************************************************/

/**
 * @brief This function handles TIM17 global interrupt.
 */
void TIM17_IRQHandler(void)
{
    //HAL_TIM_IRQHandler(&htim17);
}

/**
 * @brief This function handles I2C1 Event interrupt.
 */
void I2C1_EV_IRQHandler(void)
{
    Driver::I2c::IrqHandlerEvtPort1();
}

/**
 * @brief This function handles I2C1 Error interrupt.
 */
void I2C1_ER_IRQHandler(void)
{
    Driver::I2c::IrqHandlerErrPort1();
}

/**
 * @brief This function handles I2C2 Event interrupt.
 */
void I2C2_EV_IRQHandler(void)
{
    Driver::I2c::IrqHandlerEvtPort2();
}

/**
 * @brief This function handles I2C2 Error interrupt.
 */
void I2C2_ER_IRQHandler(void)
{
    Driver::I2c::IrqHandlerErrPort2();
}
/**
 * @brief This function handles I2C3 Event interrupt.
 */
void I2C3_EV_IRQHandler(void)
{
    Driver::I2c::IrqHandlerEvtPort3();
}

/**
 * @brief This function handles I2C3 Error interrupt.
 */
void I2C3_ER_IRQHandler(void)
{
    Driver::I2c::IrqHandlerErrPort3();
}

/**
 * @brief This function handles I2C4 Event interrupt.
 */
void I2C4_EV_IRQHandler(void)
{
    Driver::I2c::IrqHandlerEvtPort4();
}

/**
 * @brief This function handles I2C4 Error interrupt.
 */
void I2C4_ER_IRQHandler(void)
{
    Driver::I2c::IrqHandlerErrPort4();
}

/**
 * @brief This function handles USB OTG HS global interrupt.
 */
void OTG_HS_IRQHandler(void)
{
    DriverUsb_IrqHandler();
}

/**
 * @brief This function handles SDMMC1 global interrupt.
 */
void SDMMC1_IRQHandler(void)
{
    Driver::Mmc::IrqHandlerPort1();
}

/**
 * @brief This function handles RTC non-secure interrupt.
 */
void RTC_IRQHandler(void)
{
    Driver::Rtc::GetInstance().IrqHandler();
}

/**
 * @brief   This function handles EXTI Line0 interrupt.
 */
void EXTI0_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_0);
}

/**
 * @brief   This function handles EXTI Line1 interrupt.
 */
void EXTI1_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_1);
}

/**
 * @brief   This function handles EXTI Line2 interrupt.
 */
void EXTI2_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_2);
}

/**
 * @brief   This function handles EXTI Line3 interrupt.
 */
void EXTI3_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_3);
}

/**
 * @brief   This function handles EXTI Line4 interrupt.
 */
void EXTI4_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_4);
}

/**
 * @brief   This function handles EXTI Line5 interrupt.
 */
void EXTI5_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_5);
}

/**
 * @brief   This function handles EXTI Line6 interrupt.
 */
void EXTI6_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_6);
}

/**
 * @brief   This function handles EXTI Line7 interrupt.
 */
void EXTI7_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_7);
}

/**
 * @brief   This function handles EXTI Line8 interrupt.
 */
void EXTI8_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_8);
}

/**
 * @brief   This function handles EXTI Line9 interrupt.
 */
void EXTI9_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_9);
}

/**
 * @brief   This function handles EXTI Line10 interrupt.
 */
void EXTI10_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_10);
}

/**
 * @brief   This function handles EXTI Line11 interrupt.
 */
void EXTI11_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_11);
}

/**
 * @brief   This function handles EXTI Line12 interrupt.
 */
void EXTI12_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_12);
}

/**
 * @brief   This function handles EXTI Line13 interrupt.
 */
void EXTI13_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_13);
}

/**
 * @brief   This function handles EXTI Line14 interrupt.
 */
void EXTI14_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_14);
}

/**
 * @brief   This function handles EXTI Line15 interrupt.
 */
void EXTI15_IRQHandler()
{
    Driver::Gpio::IrqHandler(GPIO_PIN_15);
}

/**
 * @brief This function handles SPI1 global interrupt.
 */
void SPI1_IRQHandler(void)
{
    Driver::Spi::IrqHandlerPort1();
}

/**
 * @brief This function handles SPI2 global interrupt.
 */
void SPI2_IRQHandler(void)
{
    Driver::Spi::IrqHandlerPort2();
}

/**
 * @brief This function handles SPI3 global interrupt.
 */
void SPI3_IRQHandler(void)
{
    Driver::Spi::IrqHandlerPort3();
}


/**
 * @brief This function handles USART1 global interrupt.
 */
void USART1_IRQHandler(void)
{
    Driver::Uart::IrqHandlerPort1();
}

/**
 * @brief This function handles USART2 global interrupt.
 */
void USART2_IRQHandler(void)
{
    Driver::Uart::IrqHandlerPort2();
}

/**
 * @brief This function handles LPUART1 global interrupt.
 */
void LPUART1_IRQHandler(void)
{
    Driver::LpUart::IrqHandlerPort1();
}

/**
  * @brief This function handles TIM6 global interrupt.
  */
void TIM6_IRQHandler(void)
{
    Driver::LpUart::IrqHandlerTimPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 0 global interrupt.
 */
void GPDMA1_Channel0_IRQHandler()
{
    Driver::Spi::IrqDmaCh0HandlerPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 1 global interrupt.
 */
void GPDMA1_Channel1_IRQHandler()
{
    Driver::Spi::IrqDmaCh1HandlerPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 2 global interrupt.
 */
void GPDMA1_Channel2_IRQHandler()
{
    Driver::Spi::IrqDmaCh2HandlerPort2();
}

/**
 * @brief   This function handles GPDMA1 Channel 3 global interrupt.
 */
void GPDMA1_Channel3_IRQHandler()
{
    Driver::Spi::IrqDmaCh3HandlerPort2();
}

/**
 * @brief   This function handles GPDMA1 Channel 4 global interrupt.
 */
void GPDMA1_Channel4_IRQHandler(void)
{
    Driver::Spi::IrqDmaCh4HandlerPort3();
}

/**
 * @brief   This function handles GPDMA1 Channel 5 global interrupt.
 */
void GPDMA1_Channel5_IRQHandler(void)
{
    Driver::Spi::IrqDmaCh5HandlerPort3();
}

/**
 * @brief   This function handles GPDMA1 Channel 6 global interrupt.
 */
void GPDMA1_Channel6_IRQHandler(void)
{
    Driver::Uart::IrqDmaCh6HandlerPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 7 global interrupt.
 */
void GPDMA1_Channel7_IRQHandler(void)
{
    Driver::Uart::IrqDmaCh7HandlerPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 8 global interrupt.
 */
void GPDMA1_Channel8_IRQHandler(void)
{
    Driver::Uart::IrqDmaCh8HandlerPort2();
}

/**
 * @brief   This function handles GPDMA1 Channel 9 global interrupt.
 */
void GPDMA1_Channel9_IRQHandler(void)
{
    Driver::Uart::IrqDmaCh9HandlerPort2();
}

/**
 * @brief   This function handles GPDMA1 Channel 10 global interrupt.
 */
void GPDMA1_Channel10_IRQHandler(void)
{
    Driver::LpUart::IrqDmaCh10HandlerPort1();
}

/**
 * @brief   This function handles GPDMA1 Channel 11 global interrupt.
 */
void GPDMA1_Channel11_IRQHandler(void)
{
    Driver::LpUart::IrqDmaCh11HandlerPort1();
}


