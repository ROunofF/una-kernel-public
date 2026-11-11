/**
 ******************************************************************************
 * @file    Sleep.cpp
 * @brief   Sleep functionality.
 ******************************************************************************
 *
 ******************************************************************************
 */

// Make sure that the stack configMINIMAL_STACK_SIZE
// is sufficient to print the log (min 1024)
#define LOG_MODULE_PRX      "Sleep"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Sleep.hpp"
#include "System.hpp"

#if (USE_DEBUG_RTT == 1)
#include "SEGGER_RTT.h"
#endif


#if (USE_SYSTEM_SLEEP == 1)

// Reference timer to restore system ticks
// Defined in stm32u5xx_timebase_systick.c
extern "C" HAL_StatusTypeDef HAL_InitReferenceTim();
extern "C" uint32_t HAL_GetReferenceAbsoluteTicks();
extern "C" uint32_t HAL_GetTicksBehindReference();
extern "C" void HAL_CatchUpTicks();

namespace Driver
{

bool Sleep::Init(Driver::Rtc &rtc)
{
    if (mIsInit) {
        return true;
    }

    SLLEP_DEBUG_PIN_INIT();
    mpRtc = &rtc;
    mMaxSleepTimeMs = Driver::Sleep::mpRtc->getMaxWakeUpTime();

    mIsInit = HAL_InitReferenceTim() == HAL_OK;

    return mIsInit;
}

float Sleep::GetSleepPercentage()
{
    return (mSleepTicks * 100.0) / HAL_GetReferenceAbsoluteTicks();
}

void Sleep::AdjustTime()
{
    if (!mIsEnabled || !mIsInit) {
        return;
    }

    // The reference timer ran through the sleep; the tick did not. How far the
    // tick is behind it now is the time slept, plus whatever it was behind
    // already, so one reading covers both.
    // The scheduler is suspended until portSUPPRESS_TICKS_AND_SLEEP()
    // returns, so no other task runs before this has finished.
    const uint32_t behind = HAL_GetTicksBehindReference();

    // The kernel takes one step of at most the time it expected to sleep.
    // The sleep can run past that - the wakeup timer is armed a little after
    // the tick was stopped - and the rest is caught up one tick at a time,
    // pended while the scheduler is suspended. Left to the next SysTick it
    // would wait until the CPU next stays awake for a whole tick, and the tick
    // would fall further behind with every sleep before that.
    const uint32_t step = (behind < mExpectedIdleTicks) ? behind : mExpectedIdleTicks;

    if (step > 0) {
        vTaskStepTick(step);
        uwTick += step;
    }
    mSleepTicks += behind;
    SLEEP_TRACE(SLEEP_TRACE_ADJUST, (behind & 0xFFFF) | (step << 16));

    HAL_CatchUpTicks();
}

} // namespace Driver

#if ( configUSE_TICKLESS_IDLE == 2 )

extern "C" void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime)
{
    if (!Driver::Sleep::mIsEnabled ||
        !Driver::Sleep::mIsInit)
    {
        return;
    }
//LOG_DEBUG_WP(">> %u\n", xExpectedIdleTime);
    // Short sleep
    if (xExpectedIdleTime < 2 || Driver::Sleep::mPauseCnt > 0) {
        __WFI();
        return;
    }

#if (USE_DEBUG_RTT == 1)
    if (SEGGER_RTT_GetBytesInBuffer(0) > 0
            && SEGGER_RTT_GetAvailWriteSpace(0) > 0)
    {
        return;
    }
#endif
    // Enter a critical section that will not effect interrupts bringing
    // the MCU out of sleep mode.
    __disable_irq();
    SLEEP_TRACE(SLEEP_TRACE_ENTER, xExpectedIdleTime);

    // Stop the timer that is generating the tick interrupt.
    HAL_SuspendTick();

    // Ensure it is still ok to enter the sleep mode.
    eSleepModeStatus eSleepStatus = eTaskConfirmSleepModeStatus();

    if (eSleepStatus == eAbortSleep) {
        // A task has been moved out of the Blocked state since this macro was
        // executed, or a context siwth is being held pending.  Do not enter a
        // sleep state.  Restart the tick and exit the critical section.
        HAL_ResumeTick();
        SLEEP_TRACE(SLEEP_TRACE_ABORT, 0);
        __enable_irq();
    } else {

        // Configure an interrupt to bring the microcontroller out of its low
        // power state at the time the kernel next needs to execute.  The
        // interrupt must be generated from a source that remains operational
        // when the microcontroller is in a low power state.

        Driver::Sleep::mExpectedIdleTicks = xExpectedIdleTime;

        // RTC needs time in milliseconds
        uint32_t sleepMs = pdTICKS_TO_MS(xExpectedIdleTime);

        // Adjust the time to the maximum possible for RTC
        if (sleepMs > Driver::Sleep::mMaxSleepTimeMs) {
            sleepMs = Driver::Sleep::mMaxSleepTimeMs;
        }

        SLEEP_TRACE(SLEEP_TRACE_ARMED, sleepMs);
        if (Driver::Sleep::mpRtc->setWakeUpTimer(sleepMs)) {
            // Interrupts stay masked across the WFI. On Cortex-M a pending,
            // enabled interrupt is a wake-up event whatever PRIMASK holds, so
            // masking cannot keep the MCU asleep - it only stops the handler
            // from running before the sleep is entered, and that is the point.
            // eTaskConfirmSleepModeStatus() above certified that no task is
            // ready to run, and the certificate is worth something only while
            // nothing can make one ready. Unmasked here, an interrupt already
            // pending or arriving while the wakeup timer is armed would be
            // serviced first, and with the scheduler suspended the task it
            // readies reaches the pending-ready list and no further - nothing
            // cancels the sleep. The WFI would then hold that task until the
            // wakeup timer fires, up to mMaxSleepTimeMs later. Masked, the same
            // interrupt drops the WFI straight through and its handler runs at
            // the __enable_irq() below, with the clocks and the tick back.
            SLEEP_ENTRY_PROBE(sleepMs);

            // A tick that fell due after the mask went up is still pending,
            // and would both cut the sleep short and count once more on top of
            // the catch-up after it. The catch-up counts it from the reference
            // timer anyway. Only here: on the paths that do not sleep there is
            // no catch-up, and the pending tick runs at the unmask as usual.
            SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

            SLLEP_DEBUG_PIN_SET();
            Driver::System::Sleep();
            SLLEP_DEBUG_PIN_RESET();
            SLEEP_TRACE(SLEEP_TRACE_WOKE, 0);

            // Restore voltage regulator and clocks
            // MSI+PLL: 130 us,
            // HSI+PLL: 145 us
            // HSE+PLL: 250 - 750 us, unstable
            Driver::System::Wake();
            SLEEP_TRACE(SLEEP_TRACE_RESTORED, 0);

            Driver::Sleep::AdjustTime();
        } else {
            SLEEP_TRACE(SLEEP_TRACE_ARM_FAILED, 0);
        }

        // Restart the timer that is generating the tick interrupt.
        HAL_ResumeTick();
        SLEEP_TRACE(SLEEP_TRACE_EXIT, 0);
         __enable_irq();
    }

}

#elif ( configUSE_TICKLESS_IDLE == 1 )

extern "C" void PreSleepProcessing(uint32_t ulExpectedIdleTime)
{
}

extern "C" void PostSleepProcessing(uint32_t ulExpectedIdleTime)
{
}


#endif

#endif // (USE_SYSTEM_SLEEP == 1)

