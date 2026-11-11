/**
 ******************************************************************************
 * @file    TestSleepWakeup.cpp
 * @brief   Hardware test: does an interrupt raised on the way into STOP2 still
 *          reach the task that is waiting for it, and how soon.
 *
 *          The window under test is narrow - it opens when the wakeup timer is
 *          armed and closes at the WFI - so waiting for a real interrupt to
 *          land in it is a matter of two free-running clocks happening to
 *          agree. This test does not wait. It attaches to the sleep driver's
 *          entry probe and raises a spare vector by hand, so every round puts
 *          an interrupt exactly where the question is.
 *
 *          Setting the NVIC pending bit is the whole of what a peripheral does
 *          to reach the core, and the hook touches nothing else: not the
 *          interrupt mask, not the decision to sleep, not the wake path.
 *
 *          A round then reads three numbers. How long the wakeup timer was
 *          armed for, how soon the handler ran, and how soon the task ran. The
 *          handler is prompt either way. The task is the answer: if it runs
 *          about when the wakeup timer was due rather than at once, its wake-up
 *          was lost on the way into the sleep and the sleep ran its full
 *          course. The token is taken either way - the signal is late, not
 *          lost - so the take() result tells the two apart from a timeout.
 *
 *          A round is judged against how long the wakeup timer was armed
 *          for, so something in the system has to hold that below the wait -
 *          left alone, the waiting task is the only timeout there is, the
 *          timer is armed for exactly the wait, and a late round reads the
 *          same as one that timed out. A periodic timer supplies that bound
 *          and is the knob of the experiment: whatever it is set to, a lost
 *          wake-up costs that much, and the cost following the knob is what
 *          identifies the cause rather than the symptom.
 *
 *          Rounds are recorded and printed afterwards: a log line of its own
 *          keeps the system awake and there would be no sleep left to measure.
 *
 *          That covers the way into the sleep. The way out is a separate
 *          question - does anything run before the clocks and the tick are
 *          back - and the probe cannot answer it: its interrupt is raised
 *          before the WFI, so with the fix the WFI falls through and STOP2 is
 *          never entered at all. Nothing lowered the clocks, so a handler
 *          would find them in order either way.
 *
 *          Only an interrupt timed by hardware, arriving while the part is
 *          already asleep, comes out through the wake path, and software
 *          cannot arrange one: setting a pending bit necessarily happens
 *          before the WFI. Which timers keep running in STOP2, and whether any
 *          of them is free, is a board decision this file cannot see - so it
 *          asks nothing of them. A button needs neither. It wakes from STOP2
 *          the way the watch normally does, and a person presses it at no
 *          particular moment. SW4's handler is borrowed for the run and
 *          reports what it finds on arrival: which clock the core is on, and
 *          whether the tick has been caught up with the time really slept.
 *
 *          A run needs test mode, the probe turned on in Sleep.hpp, and nothing
 *          else in the system waking it on a period of its own - the periodic
 *          heap and stack check started at boot above all. Such a timer both
 *          shortens the sleep the rounds are judged against and lands a wake
 *          inside them, which is the one thing being measured.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSleepWake"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSleepWakeup.hpp"
#include "Sleep.hpp"
#include "Gpio.hpp"
#include "OS.hpp"

#include "main.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "stm32u5xx_ll_rcc.h"

#include <cstdint>

#if (USE_SLEEP_ENTRY_PROBE != 1)

// This file is built whether or not the test is dispatched, so the probe being
// off cannot be a compile error. Without it the sleep entry never reaches here
// and every round would report that the system did not sleep, which would be
// true of nothing - so say the real reason, once, if anyone runs it.
void TestSleepWakeup_execute()
{
    LOG_ERROR("needs USE_SLEEP_ENTRY_PROBE = 1 in Sleep.hpp\n");
}

#else

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

/// How many rounds a pass runs.
#define ROUNDS          (20U)

/// How long the task gives the token before it calls the round a miss. Well
/// over any sleep the system arms, so a round that reports the token arriving
/// late is never a round that simply timed out.
#define WAIT_MS         (10000U)

/// What holds the arming below the wait. Every round then measures against a
/// sleep that is shorter than the wait by a wide margin, and because the task
/// blocks at no fixed phase of this period, the arming differs round to round:
/// a lost wake-up has to follow it value by value, not just on average.
#define CAP_MS          (300U)

/// A round passes if the task ran this soon after the interrupt. The wake path
/// restores the clocks and the tick before any handler runs, which costs well
/// under a millisecond; the bound is loose enough not to argue about jitter.
#define PROMPT_MS       (5U)

/// The spare vector the probe raises. The peripheral is never clocked and
/// never configured - only its NVIC line is enabled - so nothing holds the
/// line asserted and entry to the handler clears the pending bit for good.
#define PROBE_IRQ       TIM7_IRQn
#define PROBE_HANDLER   TIM7_IRQHandler

/// FreeRTOS may be called from an interrupt no more urgent than this.
#define PROBE_PRIORITY  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)

/// The button borrowed for the wake half. Its handler is replaced rather than
/// chained, so for the length of the run this one stops working as a button.
#define WAKE_BUTTON     (Driver::Gpio::Pin::SW4)

/// How long each pass leaves the system asleep for a press to land in. The
/// cap timer still wakes it every CAP_MS, but those wakes are microseconds
/// long, so a press falls inside a sleep at almost any moment.
#define WAKE_WINDOW_MS  (10000U)

/// How far the tick may lag the time really slept before the catch-up is
/// judged not to have happened by the time the handler ran.
#define TICK_SLACK_MS   (2U)

/// How many edges a pass reports. The pin interrupts on both edges, so a
/// press gives one, its release another, and the bounce several more - all of
/// them real wakes, since this handler wakes nothing and the part is back
/// asleep microseconds later.
#define EDGES           (32U)

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

namespace {

/// What one round looked like.
struct Round {
    uint32_t armedMs = 0;       ///< what the wakeup timer was set for
    uint16_t isrMs   = 0;       ///< interrupt raised -> handler ran
    uint16_t taskMs  = 0;       ///< interrupt raised -> task ran
    bool     poked   = false;   ///< the sleep entry was reached at all
    bool     took    = false;   ///< the token arrived rather than timing out
};

/// What the handler found when it ran. Not called Edge: that name belongs to
/// the interrupt's own, one line away and meaning something else.
struct Wake {
    uint16_t sleptMs = 0;       ///< time really passed since the sleep entry
    uint16_t tickMs  = 0;       ///< how far the tick was carried in that time
    bool     onPll1  = false;   ///< the core was back on PLL1, not stop-range MSIS
};

} // namespace

////////////////////////////////////////////////////////////////////////////////
//// Private data
////////////////////////////////////////////////////////////////////////////////

// The reference timer: LSE driven, running through STOP2, and read as a delta
// so its 16-bit wrap cannot spoil a measurement. The system's own millisecond
// clock is no use here - it is corrected by the very path under test.
//
// It counts at 1024 Hz, so a delta has to be converted before it can be read
// against the arming, which is in real milliseconds. Left raw it would read
// 2.4 percent long and invite an argument about where the excess came from.
// The same goes for uwTick, which is a different clock counting at the same
// rate: HAL_TICKS_TO_MS is what turns either into milliseconds. Give that macro
// a single name and never an expression - it does not parenthesise what it is
// handed, so `a - b` comes out as `(uint64_t)a - b * 1000ULL`.
extern "C" uint16_t HAL_GetReferenceTicks();
extern "C" uint16_t HAL_GetReferenceTicksPassed(uint16_t FromTicks);

namespace {

/// Milliseconds since `fromTicks` was read off the reference timer.
uint16_t msSince(uint16_t fromTicks)
{
    return static_cast<uint16_t>(HAL_TICKS_TO_MS(HAL_GetReferenceTicksPassed(fromTicks)));
}

OS::Semaphore* spGate = nullptr;

/// Set by the task for one sleep entry, cleared by the entry that takes it.
volatile bool sArmed = false;

/// What the sleep entry recorded on its way past.
volatile bool     sPoked     = false;
volatile uint16_t sPokeTicks = 0;
volatile uint32_t sArmedMs   = 0;
volatile uint16_t sIsrMs     = 0;

Round sRounds[ROUNDS];

/// Where the clocks and the tick stood when the last sleep was entered. Taken
/// on every entry, armed or not: a press is not announced in advance.
volatile uint16_t sEntryRef  = 0;
volatile uint32_t sEntryTick = 0;

/// True from a sleep entry until the first edge reads it. An edge that
/// arrives while the part is already awake finds it false and is left out:
/// the anchor points at the sleep before it, so it would read a long sleep
/// with a tick that matches it and pass, diluting a real failure.
volatile bool sEntryFresh = false;

/// Edges, written by the handler and read by the task.
volatile uint32_t sEdgeCount    = 0;
volatile uint32_t sEdgeSkipped  = 0;
Wake              sEdges[EDGES];

} // namespace

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

namespace {

/// Borrows SW4 for the run. Reads the two facts that say whether the wake path
/// had already run, and nothing else: no logging, no FreeRTOS call, no waiting.
/// Both are read first, before anything here can disturb them.
class WakeProbe : public Interface::IExti::Handler
{

public:

    void extiHandler(Interface::IExti* pExti, Interface::IExti::Edge edge) override
    {
        (void) pExti;
        (void) edge;

        // Read before anything else: these are what the wake path would have
        // put right, and the question is whether it already had.
        const bool     onPll1  = (LL_RCC_GetSysClkSource()
                                     == LL_RCC_SYS_CLKSOURCE_STATUS_PLL1);
        const uint16_t slept   = msSince(sEntryRef);
        const uint32_t carried = uwTick - sEntryTick;
        const uint32_t tick    = HAL_TICKS_TO_MS(carried);

        if (!sEntryFresh) {
            sEdgeSkipped++;

            return;
        }

        sEntryFresh = false;

        const uint32_t idx = sEdgeCount;

        if (idx < EDGES) {
            sEdges[idx].onPll1  = onPll1;
            sEdges[idx].sleptMs = slept;
            sEdges[idx].tickMs  = static_cast<uint16_t>(tick);
        }

        sEdgeCount = idx + 1;
    }

};

WakeProbe sWakeProbe;

} // namespace

extern "C" void PROBE_HANDLER(void)
{
    sIsrMs = msSince(sPokeTicks);

    BaseType_t woken = pdFALSE;

    if (spGate != nullptr) {
        spGate->giveFromISR(&woken);
    }

    portYIELD_FROM_ISR(woken);
}

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

/// The sleep driver's entry probe: called once the wakeup timer is armed and
/// before the sleep is entered, which is the instant the test is about.
extern "C" void SleepEntryProbe(uint32_t armedMs)
{
    // Before the arming check: a press is not announced, so every entry has to
    // leave the anchors a handler would measure against.
    sEntryRef   = HAL_GetReferenceTicks();
    sEntryTick  = uwTick;
    sEntryFresh = true;

    if (!sArmed) {
        return;
    }

    sArmed     = false;
    sArmedMs   = armedMs;
    sPokeTicks = HAL_GetReferenceTicks();
    sPoked     = true;

    NVIC_SetPendingIRQ(PROBE_IRQ);
}

void TestSleepWakeup_execute()
{
    LOG_INFO("start\n");

    static OS::Semaphore gate(1, 0);

    spGate = &gate;

    // Nothing to do on expiry: it exists to be the next timeout in the system.
    TimerHandle_t cap = xTimerCreate("SleepCap", pdMS_TO_TICKS(CAP_MS), pdTRUE,
            nullptr, [](TimerHandle_t) { });

    if ((cap == nullptr) || (xTimerStart(cap, 0) != pdPASS)) {
        LOG_ERROR("could not hold the sleep below the wait\n");

        return;
    }

    HAL_NVIC_SetPriority(PROBE_IRQ, PROBE_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(PROBE_IRQ);

    // The pin is already up as an interrupt; only the handler is taken over, so
    // nothing here has to agree with how the button was configured. Nor is it
    // ever given back - the loop below does not return. Anything that adds a
    // way out has to hand the handler back, or the button stays taken.
    Driver::Gpio::GetInstance(WAKE_BUTTON).setExtiHandler(&sWakeProbe);

    for (;;) {

        for (uint32_t i = 0; i < ROUNDS; i++) {

            // Nothing left over from the round before.
            while (gate.take(0)) {
            }

            // Cleared together: a round the sleep entry never reached would
            // otherwise report the round before it as its own arming.
            sPoked   = false;
            sArmedMs = 0;
            sIsrMs   = 0;
            sArmed   = true;

            // The idle task cannot run while this one is ready, so the sleep
            // entry that takes the arming is the first one after this blocks.
            const bool took = gate.take(WAIT_MS);

            const uint16_t taskMs = msSince(sPokeTicks);

            sArmed = false;

            sRounds[i].poked   = sPoked;
            sRounds[i].took    = took;
            sRounds[i].armedMs = sArmedMs;
            sRounds[i].isrMs   = sIsrMs;
            sRounds[i].taskMs  = taskMs;
        }

        uint32_t prompt = 0;
        uint32_t late   = 0;
        uint32_t missed = 0;
        uint32_t awake  = 0;
        uint32_t worst  = 0;

        LOG_INFO_WP("\n---- %u rounds, the sleep held under %u ms\n",
                static_cast<unsigned>(ROUNDS), static_cast<unsigned>(CAP_MS));

        for (uint32_t i = 0; i < ROUNDS; i++) {
            const Round& r = sRounds[i];

            const char* verdict = "PROMPT";

            if (!r.poked) {
                verdict = "**NEVER SLEPT**";
                awake++;
            } else if (!r.took) {
                verdict = "**LOST**";
                missed++;
            } else if (r.taskMs > PROMPT_MS) {
                verdict = "**LATE**";
                late++;
            } else {
                prompt++;
            }

            if (r.poked && (r.taskMs > worst)) {
                worst = r.taskMs;
            }

            LOG_INFO("  round %2u: armed %5u ms, handler +%4u ms, task +%5u ms   %s\n",
                    static_cast<unsigned>(i),
                    static_cast<unsigned>(r.armedMs),
                    static_cast<unsigned>(r.isrMs),
                    static_cast<unsigned>(r.taskMs),
                    verdict);
        }

        LOG_INFO("  TOTAL prompt %2u, late %2u, lost %2u, never slept %2u, "
                 "worst task +%u ms   %s\n",
                static_cast<unsigned>(prompt),
                static_cast<unsigned>(late),
                static_cast<unsigned>(missed),
                static_cast<unsigned>(awake),
                static_cast<unsigned>(worst),
                (prompt == ROUNDS) ? "PASS" : "**FAIL**");

        //// The wake half: what a press that arrives during the sleep finds.

        sEdgeCount   = 0;
        sEdgeSkipped = 0;

        LOG_INFO_WP("\n---- press SW4 a few times over the next %u s\n",
                static_cast<unsigned>(WAKE_WINDOW_MS / 1000U));

        OS::Delay(WAKE_WINDOW_MS);

        const uint32_t seen = sEdgeCount;
        const uint32_t kept = (seen < EDGES) ? seen : EDGES;

        uint32_t woke = 0;

        for (uint32_t i = 0; i < kept; i++) {
            const Wake& p = sEdges[i];

            const uint32_t lag = (p.sleptMs > p.tickMs) ? (p.sleptMs - p.tickMs)
                                                        : (p.tickMs - p.sleptMs);
            const bool ok = p.onPll1 && (lag <= TICK_SLACK_MS);

            if (ok) {
                woke++;
            }

            LOG_INFO("  edge %2u: core on %s, slept %5u ms, tick carried "
                     "%5u ms   %s\n",
                    static_cast<unsigned>(i),
                    p.onPll1 ? "PLL1" : "stop-range MSIS",
                    static_cast<unsigned>(p.sleptMs),
                    static_cast<unsigned>(p.tickMs),
                    ok ? "WOKE FIRST" : "**RAN BEFORE THE WAKE**");
        }

        if (seen > kept) {
            LOG_WARNING("  %u edges past the %u there is room for, not shown\n",
                    static_cast<unsigned>(seen - kept),
                    static_cast<unsigned>(EDGES));
        }

        if (kept == 0) {
            LOG_WARNING("  no edge seen; the wake half says nothing\n");
        } else {
            LOG_INFO("  TOTAL %u of %u edges found the clocks and the tick "
                     "already back, %u arrived awake and were left out   %s\n",
                    static_cast<unsigned>(woke),
                    static_cast<unsigned>(kept),
                    static_cast<unsigned>(sEdgeSkipped),
                    (woke == kept) ? "PASS" : "**FAIL**");
        }
    }
}

#endif // (USE_SLEEP_ENTRY_PROBE != 1)
