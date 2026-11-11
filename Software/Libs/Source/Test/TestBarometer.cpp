/**
 ******************************************************************************
 * @file    TestBarometer.cpp
 * @brief   Hardware test: does the barometer component measure when it should,
 *          and stamp what it measured with when.
 *
 *          The part's own callback is tapped, with no sensor layer in between.
 *          Each stage runs one rung of the ladder and reports the steps
 *          between instants, and how long after its instant each measurement
 *          was handed over: the conversions, and any wait for the bus. The
 *          stages run on a quiet board held awake, on one left to sleep, and
 *          with the heart-rate sensor working on the same bus. Then a rate is
 *          changed on a running stream both ways, and a stream is stopped.
 *
 *          Nothing prints while a stage runs; a line per sample would itself
 *          make the samples late.
 *
 *          A stage fails on a measurement missing, on the wrong period handed
 *          over, or on the two channels disagreeing. The spread of the steps
 *          is printed and judged loosely: it is what the run is for.
 *
 *          The passes alternate the order of the awake and the sleeping
 *          stages, so that what differs between them is not what ran first.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestBarometer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestBarometer.hpp"
#include "System.hpp"
#include "Sleep.hpp"
#include "Gpio.hpp"
#include "OS.hpp"

#include "ComponentFactory.hpp"
#include "IBarometer.hpp"
#include "Instances/InstancePAH8316LS.hpp"

#include <cstdint>

namespace {

/// How long a stream is left stopped before the next stage starts it, so the
/// stage opens on a part that was powered down.
constexpr uint32_t kBetweenMs = 500;

/// The longest a start may take to hand over its first measurement: the
/// power-up, the calibration read and one measurement.
constexpr uint32_t kFirstSampleMs = 500;

/// How far a step may fall from its period before the stage fails: a
/// couple of milliseconds for a wake-up out of deep sleep, and as much
/// again.
constexpr uint32_t kStepSlackUs = 5000;

/// What a stretch of measurements looked like.
struct Stats {
    uint32_t pressures    = 0;
    uint32_t temperatures = 0;

    /// Pairs whose two channels did not carry one instant and one period.
    uint32_t mismatched = 0;

    /// When the first and the last were measured.
    uint64_t firstUs = 0;
    uint64_t lastUs  = 0;

    /// The steps between instants.
    uint32_t minStepUs = 0;
    uint32_t maxStepUs = 0;
    uint64_t sumStepUs = 0;
    uint32_t steps     = 0;

    /// Steps a period and a half or longer: a measurement is missing.
    uint32_t gaps = 0;

    /// How long after its instant a measurement was handed over, ms.
    uint32_t minTookMs = 0;
    uint32_t maxTookMs = 0;
    uint32_t sumTookMs = 0;

    /// The period handed over with the last measurement, and how many
    /// measurements carried a different one from the one before.
    uint32_t periodUs      = 0;
    uint32_t periodChanges = 0;

    /// The step across the last change of period.
    uint32_t changeStepUs = 0;

    float minMbar = 0.0f;
    float maxMbar = 0.0f;
    float minDegC = 0.0f;
    float maxDegC = 0.0f;
};

/// The component's callback. Measurements arrive on its thread and are read
/// on the test's, so the state is locked; nothing prints on the delivery side.
class Tap : public Interface::IBarometer::DataCallback
{

public:

    bool init()
    {
        return mMutex.init(LOG_MODULE_PRX);
    }

    /// Drop everything counted so far, the last instant included: a stage
    /// boundary is not a missing measurement.
    void restart()
    {
        OS::MutexCS lock(mMutex);

        mStats    = Stats {};
        mHaveLast = false;
    }

    Stats snapshot()
    {
        OS::MutexCS lock(mMutex);

        return mStats;
    }

    void onPressure(uint64_t timestampUs, float mbar, uint32_t periodUs) override
    {
        const uint32_t nowMs = ::Driver::System::GetTimeMs();

        OS::MutexCS lock(mMutex);

        Stats& s = mStats;

        // The instant is a whole number of milliseconds, so this is exact.
        const uint32_t tookMs = nowMs - static_cast<uint32_t>(timestampUs / 1000U);

        if ((s.pressures == 0) || (tookMs < s.minTookMs)) {
            s.minTookMs = tookMs;
        }

        if (tookMs > s.maxTookMs) {
            s.maxTookMs = tookMs;
        }

        s.sumTookMs += tookMs;

        if (mHaveLast && (timestampUs >= mLastUs)) {
            const uint32_t stepUs = static_cast<uint32_t>(timestampUs - mLastUs);

            if ((s.steps == 0) || (stepUs < s.minStepUs)) {
                s.minStepUs = stepUs;
            }

            if (stepUs > s.maxStepUs) {
                s.maxStepUs = stepUs;
            }

            s.sumStepUs += stepUs;
            s.steps++;

            if (periodUs != s.periodUs) {
                s.periodChanges++;
                s.changeStepUs = stepUs;
            } else if ((static_cast<uint64_t>(stepUs) * 2U)
                    >= (static_cast<uint64_t>(periodUs) * 3U)) {
                s.gaps++;
            }
        }

        if ((s.pressures == 0) || (mbar < s.minMbar)) {
            s.minMbar = mbar;
        }

        if ((s.pressures == 0) || (mbar > s.maxMbar)) {
            s.maxMbar = mbar;
        }

        if (s.pressures == 0) {
            s.firstUs = timestampUs;
        }

        s.lastUs   = timestampUs;
        s.periodUs = periodUs;
        s.pressures++;

        mLastUs   = timestampUs;
        mHaveLast = true;
    }

    void onTemperature(uint64_t timestampUs, float degC, uint32_t periodUs) override
    {
        OS::MutexCS lock(mMutex);

        Stats& s = mStats;

        // The other half of the pressure just taken.
        if ((timestampUs != mLastUs) || (periodUs != s.periodUs)) {
            s.mismatched++;
        }

        if ((s.temperatures == 0) || (degC < s.minDegC)) {
            s.minDegC = degC;
        }

        if ((s.temperatures == 0) || (degC > s.maxDegC)) {
            s.maxDegC = degC;
        }

        s.temperatures++;
    }

private:

    OS::Mutex mMutex;

    Stats    mStats {};
    uint64_t mLastUs   = 0;
    bool     mHaveLast = false;

};

/// Takes heart-rate events and does nothing with them: the sensor is run for
/// what it does to the bus.
class BodyTap : public Interface::IBodySensor::Callback
{
};

uint32_t gFailures = 0;

void verdict(bool ok)
{
    if (!ok) {
        gFailures++;
    }

    LOG_INFO("      %s\n", ok ? "PASS" : "**FAIL**");
}

bool supplyIsOn()
{
    return ::Driver::Gpio::GetInstance(::Driver::Gpio::Pin::ALTIMETER_EN).get();
}

/// Stop the stream and leave the part down long enough to notice.
void stop(Interface::IBarometer& baro)
{
    (void) baro.requestStop();
    OS::Delay(kBetweenMs);
}

/// One rung, from a part powered down, for this long.
void stage(const char* pName, Interface::IBarometer& baro, Tap& tap,
           uint32_t periodMs, uint32_t seconds)
{
    stop(baro);

    tap.restart();

    const uint32_t startMs  = ::Driver::System::GetTimeMs();
    const uint32_t sleptAt  = ::Driver::Sleep::GetSleepTime();
    const bool     accepted = baro.requestStart(1000.0f / static_cast<float>(periodMs));

    OS::Delay(seconds * 1000U);

    const Stats    s       = tap.snapshot();
    const uint32_t sleptMs = ::Driver::Sleep::GetSleepTime() - sleptAt;

    const uint32_t periodUs = periodMs * 1000U;

    // How many the span between the first and the last says there were.
    const uint32_t spanUs = static_cast<uint32_t>(s.lastUs - s.firstUs);
    const uint32_t want   = (s.pressures > 0)
            ? (((spanUs + (periodUs / 2U)) / periodUs) + 1U) : 0U;

    // How many the stage is long enough for: the first, handed over within
    // kStartAllowanceMs of the start, and one each period after it. A
    // stream that stopped part-way has none missing between its first and
    // its last, and too few.
    static constexpr uint32_t kStartAllowanceMs = 200U;

    const uint32_t due = (((seconds * 1000U) - kStartAllowanceMs) / periodMs) + 1U;

    const uint32_t firstMs = (s.pressures > 0)
            ? (static_cast<uint32_t>(s.firstUs / 1000U) - startMs) : 0U;

    const uint32_t meanStepUs = (s.steps > 0)
            ? static_cast<uint32_t>(s.sumStepUs / s.steps) : 0U;

    const uint32_t meanTookMs = (s.pressures > 0)
            ? (s.sumTookMs / s.pressures) : 0U;

    LOG_INFO("  %-26s %4u of %4u smp (at least %u due), gaps %u, period %u us, slept %u ms\n",
            pName,
            static_cast<unsigned>(s.pressures),
            static_cast<unsigned>(want),
            static_cast<unsigned>(due),
            static_cast<unsigned>(s.gaps),
            static_cast<unsigned>(s.periodUs),
            static_cast<unsigned>(sleptMs));

    LOG_INFO("      step %u..%u us (mean %u), handed over after %u..%u ms "
             "(mean %u), first %u ms after the start\n",
            static_cast<unsigned>(s.minStepUs),
            static_cast<unsigned>(s.maxStepUs),
            static_cast<unsigned>(meanStepUs),
            static_cast<unsigned>(s.minTookMs),
            static_cast<unsigned>(s.maxTookMs),
            static_cast<unsigned>(meanTookMs),
            static_cast<unsigned>(firstMs));

    LOG_INFO("      P %.2f..%.2f mbar, T %.2f..%.2f C\n",
            s.minMbar, s.maxMbar, s.minDegC, s.maxDegC);

    const bool stepsOk = (s.steps == 0)
            || (((s.minStepUs + kStepSlackUs) >= periodUs)
                    && (s.maxStepUs <= (periodUs + kStepSlackUs)));

    verdict(accepted
            && (s.pressures >= 2)
            && (s.pressures == want)
            && (s.pressures >= due)
            && (s.gaps == 0)
            && (s.temperatures == s.pressures)
            && (s.mismatched == 0)
            && (s.periodUs == periodUs)
            && (s.periodChanges == 0)
            && (firstMs <= kFirstSampleMs)
            && stepsOk
            && supplyIsOn());
}

/// A rate changed on a running stream, both ways. The part stays powered and
/// the next measurement is one new period after the last.
void rateChange(Interface::IBarometer& baro, Tap& tap)
{
    stop(baro);

    // Faster: the request lands 300 ms after a measurement, three new periods
    // late already, so the next one is taken at once.
    tap.restart();
    (void) baro.requestStart(1.0f);
    OS::Delay(2300);
    (void) baro.requestStart(10.0f);
    OS::Delay(1050);

    Stats s = tap.snapshot();

    LOG_INFO("  %-26s %u smp, period now %u us, changes %u, step across %u us, "
             "gaps %u\n",
            "1000 -> 100 ms, running",
            static_cast<unsigned>(s.pressures),
            static_cast<unsigned>(s.periodUs),
            static_cast<unsigned>(s.periodChanges),
            static_cast<unsigned>(s.changeStepUs),
            static_cast<unsigned>(s.gaps));

    verdict((s.periodUs == 100000U)
            && (s.periodChanges == 1)
            && (s.changeStepUs >= 200000U)
            && (s.changeStepUs <= 500000U)
            && (s.gaps == 0)
            && (s.mismatched == 0)
            && supplyIsOn());

    // Slower: the next one is a whole new period after the last.
    tap.restart();
    OS::Delay(550);
    (void) baro.requestStart(1.0f);
    OS::Delay(2500);

    s = tap.snapshot();

    LOG_INFO("  %-26s %u smp, period now %u us, changes %u, step across %u us, "
             "gaps %u\n",
            "100 -> 1000 ms, running",
            static_cast<unsigned>(s.pressures),
            static_cast<unsigned>(s.periodUs),
            static_cast<unsigned>(s.periodChanges),
            static_cast<unsigned>(s.changeStepUs),
            static_cast<unsigned>(s.gaps));

    verdict((s.periodUs == 1000000U)
            && (s.periodChanges == 1)
            && ((s.changeStepUs + kStepSlackUs) >= 1000000U)
            && (s.changeStepUs <= (1000000U + kStepSlackUs))
            && (s.gaps == 0)
            && (s.mismatched == 0)
            && supplyIsOn());
}

/// A stopped stream measures nothing and draws nothing.
void stopped(Interface::IBarometer& baro, Tap& tap)
{
    stop(baro);

    (void) baro.requestStart(10.0f);
    OS::Delay(1000);

    const bool ran = baro.isRunning() && supplyIsOn();

    (void) baro.requestStop();
    OS::Delay(200);

    tap.restart();
    OS::Delay(2000);

    const Stats s = tap.snapshot();

    LOG_INFO("  %-26s ran %u, then %u smp in 2 s, running %u, supply %u\n",
            "stopped",
            static_cast<unsigned>(ran ? 1U : 0U),
            static_cast<unsigned>(s.pressures),
            static_cast<unsigned>(baro.isRunning() ? 1U : 0U),
            static_cast<unsigned>(supplyIsOn() ? 1U : 0U));

    verdict(ran && (s.pressures == 0) && !baro.isRunning() && !supplyIsOn());
}

void quietStages(Interface::IBarometer& baro, Tap& tap, bool awake)
{
    if (awake) {
        ::Driver::Sleep::Pause();
    }

    stage(awake ? "100 ms, awake"  : "100 ms, sleeping",  baro, tap, 100, 15);
    stage(awake ? "200 ms, awake"  : "200 ms, sleeping",  baro, tap, 200, 15);
    stage(awake ? "1000 ms, awake" : "1000 ms, sleeping", baro, tap, 1000, 20);

    if (awake) {
        ::Driver::Sleep::Resume();
    }
}

} // namespace

void TestBarometer_execute()
{
    LOG_INFO("start\n");

    static Tap     tap;
    static BodyTap bodyTap;

    if (!tap.init()) {
        LOG_ERROR("tap could not be created\n");
        return;
    }

    Interface::IBarometer& baro = ComponentFactory::GetInstance().getBarometer();

    if (!baro.init()) {
        LOG_ERROR("no barometer\n");
        return;
    }

    baro.setDataCallback(&tap);

    LOG_INFO("ladder %.2f..%.2f Hz\n",
            baro.getMinSampleRateHz(), baro.getMaxSampleRateHz());

    Hardware::PAH8316LS& body = Instance::PAH8316LS::getInstance().getPAH8316LS();

    const bool bodyOk = body.init();

    if (bodyOk) {
        body.setAccRange(8);
        body.enableDataLogging(false);
        body.registerCallback(&bodyTap);
    } else {
        LOG_WARNING("no heart-rate sensor: the shared-bus stages are skipped\n");
    }

    for (uint32_t pass = 1; ; pass++) {
        gFailures = 0;

        LOG_INFO_WP("\n");
        LOG_INFO("==== pass %u ====\n", static_cast<unsigned>(pass));

        // Which of the two runs first alternates, so a difference between
        // them cannot be the order they ran in.
        const bool awakeFirst = (pass % 2U) != 0U;

        LOG_INFO("quiet board\n");
        quietStages(baro, tap, awakeFirst);
        quietStages(baro, tap, !awakeFirst);

        LOG_INFO("the slow rungs\n");
        stage("5000 ms, sleeping",  baro, tap, 5000, 16);
        stage("10000 ms, sleeping", baro, tap, 10000, 21);

        LOG_INFO("a rate change and a stop\n");
        rateChange(baro, tap);
        stopped(baro, tap);

        if (bodyOk) {
            LOG_INFO("heart-rate sensor running on the same bus\n");

            body.enable(Hardware::PAH8316LS::OperateMode::AlwaysRun);
            OS::Delay(2000);

            ::Driver::Sleep::Pause();
            stage("100 ms, awake, HR on",  baro, tap, 100, 15);
            stage("1000 ms, awake, HR on", baro, tap, 1000, 20);
            ::Driver::Sleep::Resume();

            stage("100 ms, sleeping, HR on", baro, tap, 100, 15);

            body.disable();
        }

        stop(baro);

        LOG_INFO("==== pass %u: %u failed ====\n",
                static_cast<unsigned>(pass),
                static_cast<unsigned>(gFailures));

        OS::Delay(3000);
    }
}
