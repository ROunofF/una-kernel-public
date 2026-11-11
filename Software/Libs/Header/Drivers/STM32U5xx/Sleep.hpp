/**
 ******************************************************************************
 * @file    Sleep.hpp
 * @brief   Sleep functionality.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_SLEEP_HPP
#define DRIVER_SLEEP_HPP

#include <cstdint>
#include <cstdbool>

#include "main.h"

#if (USE_SYSTEM_SLEEP == 1)
#include "FreeRTOS.h"
#include "task.h"
#include "Rtc.hpp"

// Use for debug
#define USE_SLLEP_DEBUG_PIN 0
#if (USE_SLLEP_DEBUG_PIN == 1)
#define SLLEP_DEBUG_PIN_INIT()  \
do { \
    __HAL_RCC_GPIOD_CLK_ENABLE(); \
    GPIO_InitTypeDef GPIO_InitStruct = {0}; \
    GPIO_InitStruct.Pin = GPIO_PIN_6; \
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; \
    GPIO_InitStruct.Pull = GPIO_NOPULL; \
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; \
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct); \
} while(0)

#define SLLEP_DEBUG_PIN_SET()   do {GPIOD->BSRR = GPIO_PIN_6;} while(0)
#define SLLEP_DEBUG_PIN_RESET() do {GPIOD->BRR  = GPIO_PIN_6;} while(0)
#else
#define SLLEP_DEBUG_PIN_INIT()
#define SLLEP_DEBUG_PIN_SET()
#define SLLEP_DEBUG_PIN_RESET()
#endif

// Use for test. Marks the instant between the wakeup timer being armed and the
// sleep being entered. An interrupt raised there is one the sleep must not
// outlive. Interrupts do arrive there - that is the fault this exists to catch
// - but only when two free-running clocks happen to agree, which is not
// something a test can ask for. Attaching here lets one be raised on purpose.
// Off, this is not a call that does nothing - there is no call.
#define USE_SLEEP_ENTRY_PROBE 0
#if (USE_SLEEP_ENTRY_PROBE == 1)
extern "C" void SleepEntryProbe(uint32_t armedMs);
#define SLEEP_ENTRY_PROBE(armedMs)  SleepEntryProbe(armedMs)
#else
#define SLEEP_ENTRY_PROBE(armedMs)
#endif

// Use for test. Marks each stage of the tickless path, so a test can record
// what the tick, the reference timer and the wake source read at that moment,
// with interrupts still masked. The tick is stopped and carried over the sleep
// inside this path, so only a mark inside it sees the clocks either side.
// Off, this is not a call that does nothing - there is no call.
#define USE_SLEEP_TRACE 0
#define SLEEP_TRACE_ENTER       1   // arg: expected idle ticks
#define SLEEP_TRACE_ARMED       2   // arg: ms armed on the wakeup timer
#define SLEEP_TRACE_WOKE        3   // the WFI has returned; clocks not yet restored
#define SLEEP_TRACE_RESTORED    4   // clocks restored
#define SLEEP_TRACE_ADJUST      5   // arg: ticks behind the reference | kernel step << 16
#define SLEEP_TRACE_EXIT        6   // about to unmask, after a sleep or a failed arm
#define SLEEP_TRACE_ABORT       7   // sleep declined, a task became ready
#define SLEEP_TRACE_ARM_FAILED  8   // the wakeup timer could not be armed
#if (USE_SLEEP_TRACE == 1)
extern "C" void SleepTrace(uint32_t stage, uint32_t arg);
#define SLEEP_TRACE(stage, arg)     SleepTrace((stage), (arg))
#else
#define SLEEP_TRACE(stage, arg)
#endif

// HAL tick counter
extern __IO uint32_t uwTick;

namespace Driver
{

/**
 * @brief   Sleep driver.
 * @detail  The driver uses FreeRTOS tickless idle mode.
 *          Define configUSE_TICKLESS_IDLE must be set to 2. (user defined)
 *          The driver requires an external source of millisecond intervals,
 *          such as RTC. At the moment of sleep, the counting of HAL and
 *          RTOS ticks stops. After waking up, these counters are adjusted
 *          to the actual value of sleep time.
 *          Wake-up can be due to an external interruption or an interruption
 *          from the RTC.
 */
class Sleep {

public:

    /**
     * @brief   Initialize the sleep driver with an RTC instance.
     * @param   rtc: Reference to an external `Rtc` instance used for timekeeping.
     * @retval  Initialization status.
     */
    static bool Init(Driver::Rtc &rtc);


    /**
     * @brief   Enable sleep functionality.
     * @detail  This method enables the low-power sleep mode by validating
     *          the presence of the RTC instance.
     */
    static void Enable()
    {
        mIsEnabled = mpRtc != nullptr;
    }

    /**
     * @brief   Disable sleep functionality.
     */
    static void Disable()
    {
        mIsEnabled = false;
    }

    /**
     * @brief   Temporarily pause the sleep functionality.
     * @detail  Increments the internal pause counter, which prevents the system
     *          from entering sleep mode until `Resume()` is called.
     */
    static void Pause()
    {
        // Atomic RMW - callable from both task and ISR context. PRIMASK is
        // saved/restored (not a bare __enable_irq) so it stays correct inside
        // an ISR or a nested critical section.
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (mPauseCnt < UINT32_MAX) {
            mPauseCnt++;
        }
        __set_PRIMASK(primask);
    }

    /**
      * @brief   Resume sleep functionality after a pause.
      * @detail  Decrements the internal pause counter. Sleep functionality
      *          resumes only when the counter reaches zero.
      */
    static void Resume()
    {
        // Atomic RMW - see Pause().
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (mPauseCnt > 0) {
            mPauseCnt--;
        }
        __set_PRIMASK(primask);
    }

    /**
     * @brief   Get the total accumulated sleep time.
     * @return  The total sleep duration in milliseconds since the system started.
     */
    static uint32_t GetSleepTime()
    {
        const uint32_t ticks = mSleepTicks;

        return HAL_TICKS_TO_MS(ticks);
    }

    /**
     * @brief   Get percentage of sleep time.
     * @return  Total percentage of sleep since system startup.
     */
    static float GetSleepPercentage();

private:

    /**
     * @brief   Adjust system time after waking up from sleep.
     * @detail  Brings the RTOS and HAL tick counts up to the reference timer,
     *          which ran through the sleep. Called from the tickless path with
     *          interrupts masked, before anything else is let near the clock,
     *          so no handler ever sees a tick that has not been caught up.
     * @note    Private on purpose: the friend below is the one place that
     *          reaches it, after a sleep that happened. It measures against
     *          the reference, so a call with nothing to catch up changes
     *          nothing.
     */
    static void AdjustTime();

    /// Flag indicating whether driver is properly initialized.
    static inline bool mIsInit;

    /// Flag indicating whether sleep mode is enabled.
    static inline bool mIsEnabled;

    /// Counter to track the number of active pauses.
    static inline volatile uint32_t mPauseCnt;

    /// Total time the tick has been carried over sleeps, in ticks (not ms).
    /// It also counts any lag the tick already had when a sleep began.
    static inline uint32_t mSleepTicks;

    /// Expected idle time in ticks: the most the kernel takes in one step.
    static inline uint32_t mExpectedIdleTicks;

    /// Pointer to IRtc instance.
    static inline Driver::Rtc *mpRtc;

    /// Maximum sleep time that can be processed by the RTC
    static inline uint32_t mMaxSleepTimeMs;

    /**
     * @brief Constructor.
     */
    Sleep() = default;

    /**
     * @brief Destructor.
     */
    virtual ~Sleep() = default;

    /**
     * @brief   Implementing portSUPPRESS_TICKS_AND_SLEEP()
     * @param   xExpectedIdleTime: The parameter is the time, in ticks, until
     *          the kernel next needs to execute.
     */
    friend void ::vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime);

};

} // namespace Driver

#endif // (USE_SYSTEM_SLEEP == 1)

namespace Driver
{

/**
 * @brief   RAII scope guard for the system-sleep pause.
 * @detail  Pauses sleep on construction and resumes on scope exit, so every
 *          return path releases the pause exactly once. Use for SYNCHRONOUS,
 *          scope-bounded holds (e.g. a blocking transaction). For
 *          async/event/state holds - where the pause is taken in one context
 *          and released later in a callback or on a separate event - call
 *          Sleep::Pause()/Resume() directly instead. No-op when
 *          USE_SYSTEM_SLEEP == 0.
 */
class SleepGuard
{
public:
#if (USE_SYSTEM_SLEEP == 1)
    SleepGuard()  { Sleep::Pause();  }
    ~SleepGuard() { Sleep::Resume(); }
#else
    SleepGuard()  = default;
    ~SleepGuard() = default;
#endif
    SleepGuard(const SleepGuard&)            = delete;
    SleepGuard& operator=(const SleepGuard&) = delete;
};

} // namespace Driver

#endif // DRIVER_SLEEP_HPP
