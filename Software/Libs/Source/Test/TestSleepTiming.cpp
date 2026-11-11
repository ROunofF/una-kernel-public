/**
 ******************************************************************************
 * @file    TestSleepTiming.cpp
 * @brief   Hardware test: timing of the tickless sleep, watched from outside
 *          through two spare pins and the supply current.
 *
 *          The pins are two lines the firmware leaves undriven:
 *            A = SENSORBODY_MOSI (PD6)
 *            B = SENSORBODY_SCK  (PG9)
 *          A power profiler records both beside the current, so what the
 *          firmware says about time can be checked against a clock it does
 *          not own. Pin A is also the sleep driver's debug pin
 *          (USE_SLLEP_DEBUG_PIN in Sleep.hpp); keep that off while this runs.
 *
 *          Pin check (runPinCheck, not run by default). Both pins run a fixed
 *          pattern on the tick, first with the deep sleep held off, then with
 *          it allowed:
 *            A: square wave, 5 ticks high, 5 ticks low (9.77 ms period)
 *            B: 1 tick high at every 10th rising edge of A (97.7 ms period)
 *          Each phase opens with both pins high together for 50 ticks.
 *
 *          Sleep trace. The sleep driver marks each stage of the tickless path
 *          (USE_SLEEP_TRACE in Sleep.hpp). Here each mark is recorded with the
 *          reference timer, the SysTick phase and both tick counts, and shown
 *          on the pins:
 *            A high: from entering the tickless path to the WFI returning
 *            B high: from the WFI returning to leaving the tickless path
 *          The task sleeps for N ticks per round: first N = 5 over and over,
 *          then N at random over 2..300.
 *
 *          Verdict, against the reference timer (LPTIM4, on the LSE, running
 *          through STOP2 - the same clock the tick is meant to follow):
 *            - each time the task wakes, reference - RTOS tick is within 2 of
 *              the reading taken before the first sleep: both count the same
 *              LSE ticks, but SysTick stops in STOP2 and the reference does
 *              not, so after a sleep either may step first by up to a tick,
 *              and the baseline itself is read at some phase between the two.
 *              The clock a task sees neither falls behind nor runs ahead, not
 *              even by a lag it picks up once and then keeps;
 *            - every round lasts N - 1 .. N + 2 reference ticks: both ends are
 *              read to a whole tick (+-1), and the sleep may run past its end
 *              by up to a tick. A tick left behind by one sleep and caught up
 *              after several shows as rounds too long, then one far too short.
 *          The rapid N = 5 rounds are what catches it: the task is back asleep
 *          before a SysTick could fire, so nothing but the sleep path itself
 *          can put the tick right. The pins let a power profiler check the
 *          reference from outside.
 *          Records are printed only on request (kPrintRecords): a log line of
 *          its own keeps the system awake, so all printing waits for the end.
 *
 *          A run needs test mode and USE_SLEEP_TRACE = 1 in Sleep.hpp; with
 *          it off the test only says so.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSleepTiming"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSleepTiming.hpp"
#include "Sleep.hpp"

#if (USE_SLEEP_TRACE != 1)

void TestSleepTiming_execute()
{
    LOG_ERROR("needs USE_SLEEP_TRACE = 1 in Sleep.hpp\n");
}

#else

#include "Gpio.hpp"
#include "OS.hpp"

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

namespace {

using GPIO = Driver::Gpio;

constexpr TickType_t kHalfPeriodTicks   = 5;    // A high, then A low
constexpr uint32_t   kPeriodsPerMarkB   = 10;   // B pulses on every 10th A period
constexpr TickType_t kPulseBTicks       = 1;
constexpr TickType_t kMarkerTicks       = 50;
constexpr uint32_t   kPeriodsPerPhase   = 1024; // ~10 s

constexpr uint32_t   kFixedRounds       = 300;
constexpr TickType_t kFixedTicks        = 5;
constexpr uint32_t   kRandomRounds      = 400;
constexpr TickType_t kRandomMinTicks    = 2;
constexpr TickType_t kRandomMaxTicks    = 300;

constexpr uint32_t   kStageRound        = 20;   // task: about to sleep, arg N
constexpr uint32_t   kStageBack         = 21;   // task: sleep returned, arg N
constexpr uint32_t   kStageClock        = 22;   // task: the clocks read together

constexpr uint32_t   kMinSleeps         = kFixedRounds + kRandomRounds;
constexpr bool       kPrintRecords      = false;

GPIO& pinA()
{
    return GPIO::GetInstance(GPIO::Pin::SENSORBODY_MOSI);
}

GPIO& pinB()
{
    return GPIO::GetInstance(GPIO::Pin::SENSORBODY_SCK);
}

void marker()
{
    pinA().set(true);
    pinB().set(true);
    vTaskDelay(kMarkerTicks);
    pinA().set(false);
    pinB().set(false);
    vTaskDelay(kMarkerTicks);
}

void pattern()
{
    TickType_t wake = xTaskGetTickCount();

    for (uint32_t i = 0; i < kPeriodsPerPhase; ++i) {
        const bool markB = (i % kPeriodsPerMarkB) == 0;

        pinA().set(true);
        if (markB) {
            pinB().set(true);
            xTaskDelayUntil(&wake, kPulseBTicks);
            pinB().set(false);
            xTaskDelayUntil(&wake, kHalfPeriodTicks - kPulseBTicks);
        } else {
            xTaskDelayUntil(&wake, kHalfPeriodTicks);
        }

        pinA().set(false);
        xTaskDelayUntil(&wake, kHalfPeriodTicks);
    }
}

[[maybe_unused]] void runPinCheck(const char* name)
{
    LOG_INFO("pin check, %s: start\n", name);
    marker();
    pattern();
    LOG_INFO("pin check, %s: done\n", name);
}

struct Record {
    uint8_t  stage;
    uint8_t  systick;   // SysTick->VAL, 0..31 (LSE / 32 per tick, counts down)
    uint16_t lptim;     // LPTIM4 counter, 1024 Hz
    uint32_t uwTick;
    uint32_t rtosTick;
    uint32_t arg;
};

constexpr uint32_t kCapacity = 8192;
Record            sRecords[kCapacity];
volatile uint32_t sCount;
volatile uint32_t sLost;
volatile bool     sTracing;

inline void pinSet(GPIO_TypeDef* port, uint16_t pin, bool high)
{
    if (high) {
        port->BSRR = pin;
    } else {
        port->BRR = pin;
    }
}

inline void pinASet(bool high) { pinSet(SENSORBODY_MOSI_GPIO_Port, SENSORBODY_MOSI_Pin, high); }
inline void pinBSet(bool high) { pinSet(SENSORBODY_SCK_GPIO_Port, SENSORBODY_SCK_Pin, high); }

// LPTIM4 runs on the LSE, asynchronous to the bus: a reading counts only when
// two in a row agree.
uint16_t readLptim()
{
    uint16_t a = static_cast<uint16_t>(LPTIM4->CNT);
    for (int i = 0; i < 8; ++i) {
        const uint16_t b = static_cast<uint16_t>(LPTIM4->CNT);
        if (a == b) {
            return a;
        }
        a = b;
    }
    return a;
}

// The first two enabled interrupts pending, low byte first; 0xFF for none.
uint32_t pendingIrqs()
{
    uint32_t found[2] = {0xFF, 0xFF};
    uint32_t n = 0;

    for (uint32_t w = 0; w < 5 && n < 2; ++w) {
        uint32_t bits = NVIC->ISPR[w] & NVIC->ISER[w];
        while (bits != 0 && n < 2) {
            const uint32_t bit = __builtin_ctz(bits);
            found[n++] = w * 32 + bit;
            bits &= bits - 1;
        }
    }

    return found[0] | (found[1] << 8);
}

void record(uint32_t stage, uint32_t arg)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    const uint32_t i = sCount;
    if (i < kCapacity) {
        Record& r  = sRecords[i];
        r.stage    = static_cast<uint8_t>(stage);
        r.systick  = static_cast<uint8_t>(SysTick->VAL);
        r.lptim    = readLptim();
        r.uwTick   = uwTick;
        r.rtosTick = xTaskGetTickCountFromISR();
        r.arg      = arg;
        sCount     = i + 1;
    } else {
        sLost = sLost + 1;
    }

    __set_PRIMASK(primask);
}

uint32_t sRandom = 0x326u;

uint32_t nextRandom()
{
    sRandom = sRandom * 1664525u + 1013904223u;
    return sRandom >> 8;
}

void sleepRound(TickType_t n)
{
    record(kStageRound, n);
    vTaskDelay(n);
    record(kStageBack, n);
    record(kStageClock, 0);
}

void judge(uint32_t count)
{
    constexpr int32_t kClockTolerance = 2;

    uint32_t sleeps   = 0;
    uint32_t wakes    = 0;
    int32_t  clockMin = INT32_MAX;
    int32_t  clockMax = INT32_MIN;
    uint16_t clockBaseline    = 0;
    bool     clockBaselineSet = false;
    uint32_t rounds   = 0;
    uint32_t roundBad = 0;
    int32_t  overMin  = INT32_MAX;
    int32_t  overMax  = INT32_MIN;
    uint16_t roundStart = 0;

    for (uint32_t i = 0; i < count; ++i) {
        const Record& r = sRecords[i];

        if (r.stage == SLEEP_TRACE_EXIT) {
            ++sleeps;
        } else if (r.stage == kStageClock) {
            // Reference - RTOS tick, both read in one record with interrupts
            // masked. Taken modulo 2^16, where the reference counter wraps,
            // and against the reading before the first sleep, so neither the
            // wrap nor the constant offset between the two counts matters.
            const uint16_t raw = static_cast<uint16_t>(r.lptim - static_cast<uint16_t>(r.rtosTick));
            const int32_t clock = static_cast<int16_t>(static_cast<uint16_t>(raw - clockBaseline));
            ++wakes;
            clockMin = (clock < clockMin) ? clock : clockMin;
            clockMax = (clock > clockMax) ? clock : clockMax;
        } else if (r.stage == kStageRound) {
            roundStart = r.lptim;
            if (!clockBaselineSet) {
                clockBaseline    = static_cast<uint16_t>(r.lptim - static_cast<uint16_t>(r.rtosTick));
                clockBaselineSet = true;
            }
        } else if (r.stage == kStageBack) {
            const int32_t over = static_cast<int32_t>(static_cast<uint16_t>(r.lptim - roundStart))
                                 - static_cast<int32_t>(r.arg);
            ++rounds;
            overMin = (over < overMin) ? over : overMin;
            overMax = (over > overMax) ? over : overMax;
            if (over < -1 || over > 2) {
                ++roundBad;
            }
        }
    }

    const bool enough   = (sleeps >= kMinSleeps) && (sLost == 0);
    const bool clockOk  = (wakes > 0) && clockBaselineSet
                          && (clockMin >= -kClockTolerance) && (clockMax <= kClockTolerance);

    LOG_INFO("[clock] %u wakes, reference - RTOS tick vs before the first sleep %d..%d, "
             "within +-%d -> %s\n",
             static_cast<unsigned>(wakes), static_cast<int>(clockMin), static_cast<int>(clockMax),
             static_cast<int>(kClockTolerance),
             clockOk ? "PASS" : "FAIL");
    LOG_INFO("[rounds] %u of them, reference ticks past N %d..%d, out of -1..2: %u -> %s\n",
             static_cast<unsigned>(rounds), static_cast<int>(overMin), static_cast<int>(overMax),
             static_cast<unsigned>(roundBad), (roundBad == 0) ? "PASS" : "FAIL");
    if (!enough) {
        LOG_INFO("[coverage] %u sleeps (need %u), %u records lost -> FAIL\n",
                 static_cast<unsigned>(sleeps), static_cast<unsigned>(kMinSleeps),
                 static_cast<unsigned>(sLost));
    }

    const uint32_t failures = (clockOk ? 0 : 1) + (roundBad != 0) + (enough ? 0 : 1);
    LOG_INFO("Sleep timing test completed: %u failure(s)\n", static_cast<unsigned>(failures));
}

void runTrace()
{
    LOG_INFO("trace: %u rounds of %u ticks, then %u rounds of %u..%u\n",
             static_cast<unsigned>(kFixedRounds), static_cast<unsigned>(kFixedTicks),
             static_cast<unsigned>(kRandomRounds), static_cast<unsigned>(kRandomMinTicks),
             static_cast<unsigned>(kRandomMaxTicks));
    OS::Delay(200);     // let the line out before the first round

    sCount   = 0;
    sLost    = 0;
    sTracing = true;

    for (uint32_t i = 0; i < kFixedRounds; ++i) {
        sleepRound(kFixedTicks);
    }
    for (uint32_t i = 0; i < kRandomRounds; ++i) {
        sleepRound(kRandomMinTicks + nextRandom() % (kRandomMaxTicks - kRandomMinTicks + 1));
    }

    sTracing = false;
    pinASet(false);
    pinBSet(false);

    const uint32_t count = sCount;
    LOG_INFO("trace: %u records, %u lost\n", static_cast<unsigned>(count), static_cast<unsigned>(sLost));

    judge(count);

    if (!kPrintRecords) {
        return;
    }

    LOG_INFO_WP("TRACE BEGIN\n");
    for (uint32_t i = 0; i < count; ++i) {
        const Record& r = sRecords[i];
        LOG_INFO_WP("T %u %u %u %u %u %u\n",
                    static_cast<unsigned>(r.stage), static_cast<unsigned>(r.systick),
                    static_cast<unsigned>(r.lptim), static_cast<unsigned>(r.uwTick),
                    static_cast<unsigned>(r.rtosTick), static_cast<unsigned>(r.arg));
        if ((i % 16) == 15) {
            OS::Delay(20);
        }
    }
    LOG_INFO_WP("TRACE END\n");
}

} // namespace

extern "C" void SleepTrace(uint32_t stage, uint32_t arg)
{
    if (!sTracing) {
        return;
    }

    switch (stage) {
        case SLEEP_TRACE_ENTER:
            pinASet(true);
            break;
        case SLEEP_TRACE_WOKE:
            pinASet(false);
            pinBSet(true);
            arg = pendingIrqs();
            break;
        case SLEEP_TRACE_EXIT:
        case SLEEP_TRACE_ABORT:
            pinASet(false);
            pinBSet(false);
            break;
        default:
            break;
    }

    record(stage, arg);
}

void TestSleepTiming_execute()
{
    pinA().init(GPIO::Type::PP, false);
    pinB().init(GPIO::Type::PP, false);

    runTrace();

    // Back to what main() left them as.
    pinA().init(GPIO::Pull::NO_PULL);
    pinB().init(GPIO::Pull::NO_PULL);

    LOG_INFO("done\n");
}

#endif // (USE_SLEEP_TRACE != 1)
