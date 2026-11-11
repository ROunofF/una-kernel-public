/**
 ******************************************************************************
 * @file    TestSensorMagnetometer.cpp
 * @brief   Hardware test: does the magnetometer deliver every sample, at the
 *          rate that was asked for.
 *
 *          Phase A taps the part's own callback on a quiet board, before the
 *          sensor layer exists: the baseline. Phase C repeats it with the
 *          system up, separating the cost of a running system from the
 *          layer's. Phase B goes through the sensor layer: each channel alone
 *          at each rate, the fastest rung batched and not, both channels, two
 *          subscribers on one channel, and a live rate change both ways. A
 *          loss in B that C did not show is the layer's bug.
 *
 *          Only counts are printed; a line per sample would itself make the
 *          samples late.
 *
 *          The verdict comes from the steps between sensortime instants: a
 *          step half a period out means a lost conversion. The total count is
 *          a cross-check against the nominal rate, which the part's oscillator
 *          may miss. A part running faster than asked is not a fault. The field
 *          magnitude is a sanity check only.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSensorMag"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorMagnetometer.hpp"
#include "SwTimer.hpp"
#include "System.hpp"
#include "Sleep.hpp"
#include "OS.hpp"

#include "ComponentFactory.hpp"
#include "IMagnetometer.hpp"
#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticField.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticFieldRaw.hpp"

#include <cmath>
#include <cstdint>

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

/// How often a running stage folds up what it has, and how long a stage lasts.
#define WINDOW_MS       (3000U)
#define STAGE_S         (12U)

/// The stage rates: the fastest rung, a fit or log rate, a screen rate.
#define PERIOD_FAST_MS  (2.5f)
#define PERIOD_MID_MS   (10.0f)
#define PERIOD_SLOW_MS  (100.0f)

/// A latency that lets a window of samples arrive together.
#define LATENCY_MS      (50U)

/// No latency: one sample per delivery.
#define LATENCY_NONE    (0U)

/// Bands of how far a step fell from the requested period, as fractions of it
/// so a band means the same at every rung. The last band, half a period or
/// more, is a lost conversion and not jitter.
#define DEV_BUCKETS     (5U)

/// A step past both is a stall, not jitter: a fixed threshold alone flags
/// slow rungs, a multiple of the period alone flags fast ones.
#define STALL_US        (20000U)
#define STALL_PERIODS   (3U)

/// How long to wait for an asynchronous start to take effect.
#define START_WAIT_MS   (1000U)

/// How long to leave the system alone after the sensor layer brings the other
/// sensors up, so the next stage is not measured against their start-up.
#define SETTLE_MS       (3000U)

/// How long to leave the part alone between two stages of phase B, so that a
/// subscription taken down has finished coming down before the next goes up.
#define BETWEEN_MS      (1500U)

/// How long a rate change takes to become the stream's cadence. Samples at the
/// old rate are still on the way out after the request, and a stage counting
/// them would report a gap in a stream that lost nothing.
#define SWITCH_MS       (500U)

/// Hold the system out of deep sleep for the whole run, so what is measured is
/// the part, the driver and the layer. Pausing sleep, unlike building with
/// SLEEP_ON 0, keeps the shipped sleep path and removes only the STOP2 entry.
#define HOLD_AWAKE      (1)

/// Whether a stage prints as it goes. Off: a log line blocks the calling task
/// and puts a hole in the measurement it reports.
#define WINDOW_LINES    (0)

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

/// What a stretch of samples looked like.
struct Stats {
    uint32_t samples    = 0;    ///< how many arrived
    uint16_t maxBatch   = 0;    ///< most samples handed over in one call
    uint32_t maxGapUs   = 0;    ///< widest step between two measurements
    uint32_t stalls     = 0;    ///< steps too wide to be jitter
    uint32_t minGapUs   = 0;    ///< closest two measurements ever came

    /// How many steps fell in each band.
    uint32_t dev[DEV_BUCKETS] = {};
    float    sumMag     = 0.0f;
    bool     calibrated = false;

    /// When the first and the last were measured; the span sets how many
    /// there should have been.
    uint64_t firstUs = 0;
    uint64_t lastUs  = 0;
    bool     haveAny = false;

    void note(uint64_t timestampUs)
    {
        if (!haveAny) {
            firstUs = timestampUs;
            haveAny = true;
        }

        lastUs = timestampUs;
    }

    void fold(const Stats& other)
    {
        samples += other.samples;
        sumMag  += other.sumMag;
        stalls  += other.stalls;

        if (other.maxBatch > maxBatch) {
            maxBatch = other.maxBatch;
        }

        if (other.maxGapUs > maxGapUs) {
            maxGapUs = other.maxGapUs;
        }

        if ((minGapUs == 0) || ((other.minGapUs != 0)
                && (other.minGapUs < minGapUs))) {
            minGapUs = other.minGapUs;
        }

        for (uint32_t i = 0; i < DEV_BUCKETS; i++) {
            dev[i] += other.dev[i];
        }

        if (other.haveAny) {
            note(other.firstUs);
            lastUs = other.lastUs;
        }

        calibrated = other.calibrated;
    }

    /// How many the span says should have arrived at this period.
    uint32_t expected(float periodMs) const
    {
        const uint32_t periodUs = static_cast<uint32_t>(periodMs * 1000.0f);

        if (!haveAny || (periodUs == 0) || (lastUs <= firstUs)) {
            return samples;
        }

        const uint32_t spanUs = static_cast<uint32_t>(lastUs - firstUs);

        return ((spanUs + (periodUs / 2U)) / periodUs) + 1U;
    }

    /// What the span says is missing. Never negative: running faster than
    /// asked is allowed.
    uint32_t missing(float periodMs) const
    {
        const uint32_t want = expected(periodMs);

        return (want > samples) ? (want - samples) : 0U;
    }
};

/// The counting, and the printing of it, shared by both taps so their numbers
/// mean the same. Samples arrive on another thread and reports run on this
/// test's, so the state is locked; nothing prints on the delivery side.
class Tally
{

public:

    explicit Tally(const char* pName)
        : mpName(pName)
    {
    }

    bool init()
    {
        return mMutex.init(LOG_MODULE_PRX);
    }

    /// Begin counting against a new period, dropping everything before it,
    /// the last timestamp included: a stage boundary is not a dropout.
    void restart(float periodMs)
    {
        OS::MutexCS lock(mMutex);

        mPeriodMs      = periodMs;
        mHaveLast      = false;
        mWindow        = Stats {};
        mStage         = Stats {};
        mWindowStartMs = ::Driver::System::GetTimeMs();
        mSleepAtStart  = ::Driver::Sleep::GetSleepTime();
        mStageSleepMs  = 0;
    }

    void addBatch(uint16_t size)
    {
        OS::MutexCS lock(mMutex);

        if (size > mWindow.maxBatch) {
            mWindow.maxBatch = size;
        }
    }

    void add(uint64_t timestampUs, float x, float y, float z,
             bool calibrated = false)
    {
        OS::MutexCS lock(mMutex);

        // The widest step tells one long stall from a trickle of losses.
        if (mHaveLast && (timestampUs >= mLastUs)) {
            const uint32_t gapUs =
                    static_cast<uint32_t>(timestampUs - mLastUs);

            if (gapUs > mWindow.maxGapUs) {
                mWindow.maxGapUs = gapUs;
            }

            if ((mWindow.minGapUs == 0) || (gapUs < mWindow.minGapUs)) {
                mWindow.minGapUs = gapUs;
            }

            // Against the requested period, so a uniformly off cadence shows.
            const uint32_t periodUs =
                    static_cast<uint32_t>(mPeriodMs * 1000.0f);

            const uint32_t offUs = (gapUs > periodUs) ? (gapUs - periodUs)
                                                      : (periodUs - gapUs);

            if (offUs <= (periodUs / 512U)) {
                mWindow.dev[0]++;
            } else if (offUs <= (periodUs / 64U)) {
                mWindow.dev[1]++;
            } else if (offUs <= (periodUs / 8U)) {
                mWindow.dev[2]++;
            } else if (offUs < (periodUs / 2U)) {
                mWindow.dev[3]++;
            } else {
                mWindow.dev[4]++;
            }

            if ((gapUs > STALL_US)
                    && (gapUs > (STALL_PERIODS * periodUs))) {
                mWindow.stalls++;
            }
        }

        mLastUs   = timestampUs;
        mHaveLast = true;

        mWindow.note(timestampUs);
        mWindow.calibrated = calibrated;

        mWindow.sumMag += std::sqrt((x * x) + (y * y) + (z * z));
        mWindow.samples++;
    }

    /// Fold up what arrived since the last call and start a new window.
    void report()
    {
        Stats    window {};
        float    periodMs  = 0.0f;
        uint32_t elapsedMs = 0;
        uint32_t sleptMs   = 0;

        {
            OS::MutexCS lock(mMutex);

            window    = mWindow;
            periodMs  = mPeriodMs;
            elapsedMs = ::Driver::System::GetTimeMs() - mWindowStartMs;

            const uint32_t sleepNow = ::Driver::Sleep::GetSleepTime();

            sleptMs = sleepNow - mSleepAtStart;

            mStageSleepMs += sleptMs;

            mStage.fold(mWindow);

            mWindow        = Stats {};
            mWindowStartMs = ::Driver::System::GetTimeMs();
            mSleepAtStart  = sleepNow;
        }

        if (elapsedMs == 0) {
            return;
        }

#if (WINDOW_LINES)
        const float rate = (static_cast<float>(window.samples) * 1000.0f)
                         / static_cast<float>(elapsedMs);

        const float wanted = (periodMs > 0.0f) ? (1000.0f / periodMs) : 0.0f;

        const float meanMag = (window.samples > 0)
                ? (window.sumMag / static_cast<float>(window.samples))
                : 0.0f;

        LOG_INFO("  %-6s %5u smp  %6.1f Hz (want %6.1f)  lost %4u  "
                 "batch<=%3u  stalls %2u  gapmax %6u us  slept %5u ms  "
                 "|B| %6.2f uT\n",
                mpName,
                static_cast<unsigned>(window.samples),
                rate, wanted,
                static_cast<unsigned>(window.missing(periodMs)),
                static_cast<unsigned>(window.maxBatch),
                static_cast<unsigned>(window.stalls),
                static_cast<unsigned>(window.maxGapUs),
                static_cast<unsigned>(sleptMs),
                meanMag);
#else
        (void) window;
        (void) periodMs;
        (void) sleptMs;
#endif
    }

    /// Print the whole stage and say whether anything was lost.
    void summary()
    {
        Stats    stage {};
        float    periodMs = 0.0f;
        uint32_t sleptMs  = 0;

        {
            OS::MutexCS lock(mMutex);

            stage    = mStage;
            periodMs = mPeriodMs;
            sleptMs  = mStageSleepMs;
        }

        const uint32_t want    = stage.expected(periodMs);
        const uint32_t missing = stage.missing(periodMs);

        // The verdict follows the half-period band, not the count, which is
        // measured against the nominal period the oscillator may miss.
        const bool clean = (stage.samples > 0)
                        && (stage.dev[DEV_BUCKETS - 1U] == 0);

        const float lostPct = (want > 0)
                ? ((static_cast<float>(missing) * 100.0f)
                        / static_cast<float>(want))
                : 0.0f;

        LOG_INFO("  %-6s TOTAL %6u of %6u smp, lost %5u (%5.2f%%), "
                 "batch<=%3u, stalls %3u, gapmax %6u us, slept %5u ms, "
                 "cal=%u   %s\n",
                mpName,
                static_cast<unsigned>(stage.samples),
                static_cast<unsigned>(want),
                static_cast<unsigned>(missing),
                lostPct,
                static_cast<unsigned>(stage.maxBatch),
                static_cast<unsigned>(stage.stalls),
                static_cast<unsigned>(stage.maxGapUs),
                static_cast<unsigned>(sleptMs),
                static_cast<unsigned>(stage.calibrated ? 1U : 0U),
                clean ? "PASS" : "**LOSS**");

        const uint32_t periodUs = static_cast<uint32_t>(periodMs * 1000.0f);

        // No label may start with '<' and a letter: a log viewer reads it as
        // a tag and hides the rest of the line.
        LOG_INFO("  %-6s STEPS off by %5u us:%5u  %5u us:%5u  %5u us:%5u  "
                 "half:%u  GAPS:%u   period %u us, seen %u..%u\n",
                mpName,
                static_cast<unsigned>(periodUs / 512U),
                static_cast<unsigned>(stage.dev[0]),
                static_cast<unsigned>(periodUs / 64U),
                static_cast<unsigned>(stage.dev[1]),
                static_cast<unsigned>(periodUs / 8U),
                static_cast<unsigned>(stage.dev[2]),
                static_cast<unsigned>(stage.dev[3]),
                static_cast<unsigned>(stage.dev[4]),
                static_cast<unsigned>(periodUs),
                static_cast<unsigned>(stage.minGapUs),
                static_cast<unsigned>(stage.maxGapUs));
    }

private:

    const char* const mpName;

    OS::Mutex mMutex;

    Stats    mWindow {};
    Stats    mStage {};
    uint32_t mWindowStartMs = 0;
    float    mPeriodMs      = 0.0f;

    /// What the system's sleep counter read when the window opened, and how
    /// much of the whole stage was spent asleep.
    uint32_t mSleepAtStart = 0;
    uint32_t mStageSleepMs = 0;

    uint64_t mLastUs   = 0;
    bool     mHaveLast = false;

};

/// Phases A and C: the part's own callback, with nothing in between.
class DirectTap : public Interface::IMagnetometer::DataCallback
{

public:

    explicit DirectTap(const char* pName) : mTally(pName) {}

    Tally& tally() { return mTally; }

    void onMagnetic(uint64_t timestampUs, float x, float y, float z, uint32_t periodUs) override
    {
        (void)periodUs;

        // One sample per call: the part has no FIFO.
        mTally.addBatch(1);
        mTally.add(timestampUs, x, y, z);
    }

private:

    Tally mTally;

};

/// Phase B: one subscription through the sensor layer.
class Channel : public SDK::Interface::ISensorDataListener
{

public:

    Channel(SDK::Sensor::Type type, const char* pName)
        : mConnection(type, this)
        , mType(type)
        , mTally(pName)
    {
    }

    Tally& tally() { return mTally; }

    bool init()
    {
        return mTally.init();
    }

    /// Subscribe, or change the rate of a subscription that is already up.
    bool start(float periodMs, uint32_t latencyMs)
    {
        mTally.restart(periodMs);

        if (!mConnection.connect(periodMs, latencyMs)) {
            LOG_ERROR("could not subscribe\n");

            return false;
        }

        return true;
    }

    void stop()
    {
        mConnection.disconnect();
    }

    //// ISensorDataListener
    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        if (!mConnection.matchesDriver(handle)) {
            return;
        }

        SDK::Sensor::DataBatch batch(base, count, stride);

        mTally.addBatch(batch.size());

        for (uint16_t idx = 0; idx < batch.size(); ++idx) {
            if (mType == SDK::Sensor::Type::MAGNETIC_FIELD) {
                const SDK::SensorDataParser::MagneticField p(batch[idx]);

                mTally.add(p.getTimestampUs(), p.getX(), p.getY(), p.getZ(),
                        p.isCalibrated());
            } else {
                const SDK::SensorDataParser::MagneticFieldRaw p(batch[idx]);

                mTally.add(p.getTimestampUs(), p.getX(), p.getY(), p.getZ());
            }
        }
    }

private:

    Sensor::Connection      mConnection;
    const SDK::Sensor::Type mType;

    Tally mTally;

};

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

/// Holds the system awake for as long as it is in scope: idle becomes a
/// wait-for-interrupt with the tick left running.
class AwakeGuard
{

public:

    explicit AwakeGuard(bool hold) : mHeld(hold)
    {
        if (mHeld) {
            ::Driver::Sleep::Pause();
        }
    }

    ~AwakeGuard()
    {
        if (mHeld) {
            ::Driver::Sleep::Resume();
        }
    }

    AwakeGuard(const AwakeGuard&)            = delete;
    AwakeGuard& operator=(const AwakeGuard&) = delete;

private:

    const bool mHeld;

};

static void banner(const char* pTitle)
{
    LOG_INFO_WP("\n");
    LOG_INFO_WP("---- %s\n", pTitle);
}

/// Watch whatever is running for a while, folding up what arrives as it goes.
static void observe(uint32_t seconds, Tally* pFirst, Tally* pSecond)
{
    ::Driver::SwTimer stage(seconds * 1000U);

    for (;;) {
        OS::Delay(WINDOW_MS);

        if (pFirst != nullptr) {
            pFirst->report();
        }

        if (pSecond != nullptr) {
            pSecond->report();
        }

        if (stage.expired()) {
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//// Phases A and C - the part on its own
////////////////////////////////////////////////////////////////////////////////

/// Run the part at one rung with nothing between it and the counter.
static void directStage(Interface::IMagnetometer& part, DirectTap& tap,
        const char* pTitle, float rateHz)
{
    banner(pTitle);

    tap.tally().restart(1000.0f / rateHz);

    // Registered only for the stage, so nothing lands in a reset counter.
    part.setDataCallback(&tap);

    if (!part.requestStart(rateHz)) {
        LOG_ERROR("the part refused to start\n");

        part.setDataCallback(nullptr);

        return;
    }

    // Asynchronous: the request is posted and handled on the part's worker.
    ::Driver::SwTimer waiting(START_WAIT_MS);

    while (!part.isRunning() && !waiting.expired()) {
        OS::Delay(10);
    }

    if (!part.isRunning()) {
        LOG_ERROR("the part did not come up\n");

        part.setDataCallback(nullptr);

        return;
    }

    observe(STAGE_S, &tap.tally(), nullptr);

    tap.tally().summary();

    (void) part.requestStop();

    OS::Delay(200);

    part.setDataCallback(nullptr);
}

/// The part on a quiet board, with no sensor layer anywhere: the baseline.
static bool phaseDirect(DirectTap& tap)
{
    ComponentFactory& factory = ComponentFactory::GetInstance();

    if (!factory.isMagnetometerPresent()) {
        LOG_ERROR("no magnetometer on this board\n");

        return false;
    }

    Interface::IMagnetometer& part = factory.getMagnetometer();

    if (!part.init()) {
        LOG_ERROR("the part would not come up\n");

        return false;
    }

    LOG_INFO_WP("\n===== PHASE A: the part itself, quiet board =====\n");

    directStage(part, tap, "A1  the part at 400 Hz", 400.0f);
    directStage(part, tap, "A2  the part at 100 Hz", 100.0f);
    directStage(part, tap, "A3  the part at  25 Hz",  25.0f);

    // Leave the callback slot empty for the sensor layer to take.
    part.setDataCallback(nullptr);

    (void) part.requestStop();

    OS::Delay(200);

    return true;
}

/// The part directly, with the whole system up around it: separates what the
/// layer costs from what the system costs.
static void phaseDirectLoaded(DirectTap& tap)
{
    Interface::IMagnetometer& part =
            ComponentFactory::GetInstance().getMagnetometer();

    LOG_INFO_WP("\n===== PHASE C: the part itself, system up =====\n");

    directStage(part, tap, "C1  the part at 400 Hz, system up", 400.0f);
    directStage(part, tap, "C2  the part at 100 Hz, system up", 100.0f);

    // Give the slot back to the sensor layer, or phase B receives nothing.
    part.setDataCallback(
            &Instance::SensorLayer::getInstance().getSensorMagnetometer());
}

////////////////////////////////////////////////////////////////////////////////
//// Phase B - through the sensor layer
////////////////////////////////////////////////////////////////////////////////

/// One channel, one set of parameters, start to verdict.
static void stageOne(const char* pTitle, Channel& channel,
        float periodMs, uint32_t latencyMs)
{
    banner(pTitle);

    if (!channel.start(periodMs, latencyMs)) {
        return;
    }

    observe(STAGE_S, &channel.tally(), nullptr);

    channel.tally().summary();
    channel.stop();

    OS::Delay(BETWEEN_MS);
}

/// Two channels at once, start to verdict. The faster starts first, so the
/// slower joins a running part and has to be thinned to its own rate.
static void stageTwo(const char* pTitle,
        Channel& fast, float fastPeriodMs, uint32_t fastLatencyMs,
        Channel& slow, float slowPeriodMs, uint32_t slowLatencyMs)
{
    banner(pTitle);

    const bool fastUp = fast.start(fastPeriodMs, fastLatencyMs);
    const bool slowUp = slow.start(slowPeriodMs, slowLatencyMs);

    if (!fastUp && !slowUp) {
        return;
    }

    observe(STAGE_S, fastUp ? &fast.tally() : nullptr,
            slowUp ? &slow.tally() : nullptr);

    if (fastUp) {
        fast.tally().summary();
    }

    if (slowUp) {
        slow.tally().summary();
    }

    if (slowUp) {
        slow.stop();
    }

    if (fastUp) {
        fast.stop();
    }

    OS::Delay(BETWEEN_MS);
}

/// A rate changed underneath a live subscription rather than by stopping and
/// starting. Each half is judged separately.
static void stageRateChange(const char* pTitle, Channel& channel,
        float firstMs, uint32_t firstLatencyMs,
        float thenMs,  uint32_t thenLatencyMs)
{
    banner(pTitle);

    if (!channel.start(firstMs, firstLatencyMs)) {
        return;
    }

    observe(STAGE_S / 2U, &channel.tally(), nullptr);

    channel.tally().summary();

    LOG_INFO_WP("     ... now asking the same subscription for %.1f ms\n",
            thenMs);

    // start() on a live connection renegotiates it in place; the counter is
    // armed again once the stream has taken the new rate.
    if (channel.start(thenMs, thenLatencyMs)) {
        OS::Delay(SWITCH_MS);

        channel.tally().restart(thenMs);

        observe(STAGE_S / 2U, &channel.tally(), nullptr);

        channel.tally().summary();
    }

    channel.stop();

    OS::Delay(BETWEEN_MS);
}

static void phaseLayer(Channel& field, Channel& rawA, Channel& rawB)
{
    LOG_INFO_WP("\n===== PHASE B: through the sensor layer =====\n");

    //// One channel at a time, at each rate a consumer would ask for.

    stageOne("B1  FIELD alone, 100 ms - what a screen asks for",
            field, PERIOD_SLOW_MS, LATENCY_NONE);

    stageOne("B2  FIELD alone, 10 ms - the corrected channel's floor",
            field, PERIOD_MID_MS, LATENCY_NONE);

    stageOne("B3  RAW alone, 100 ms",
            rawA, PERIOD_SLOW_MS, LATENCY_NONE);

    stageOne("B4  RAW alone, 10 ms",
            rawA, PERIOD_MID_MS, LATENCY_NONE);

    //// The fastest rung, batched and not.

    stageOne("B5  RAW alone, 2.5 ms, latency 50 ms - 400 Hz in batches",
            rawA, PERIOD_FAST_MS, LATENCY_MS);

    stageOne("B6  RAW alone, 2.5 ms, no latency - 400 Hz one at a time",
            rawA, PERIOD_FAST_MS, LATENCY_NONE);

    //// Both channels at two rates: each subscriber is thinned to its own.

    stageTwo("B7  RAW 400 Hz + FIELD 10 Hz - the widest spread",
            rawA, PERIOD_FAST_MS, LATENCY_MS,
            field, PERIOD_SLOW_MS, LATENCY_NONE);

    stageTwo("B8  RAW 400 Hz + FIELD 100 Hz",
            rawA, PERIOD_FAST_MS, LATENCY_MS,
            field, PERIOD_MID_MS, LATENCY_NONE);

    //// Two subscribers on one channel.

    stageTwo("B9  RAW 400 Hz + RAW 10 Hz - two readers of one channel",
            rawA, PERIOD_FAST_MS, LATENCY_MS,
            rawB, PERIOD_SLOW_MS, LATENCY_NONE);

    //// A rate changed under a live subscription, both ways and on both
    //// channels.

    stageRateChange("B10 RAW 100 ms, then 2.5 ms without disconnecting",
            rawA, PERIOD_SLOW_MS, LATENCY_NONE,
            PERIOD_FAST_MS, LATENCY_MS);

    stageRateChange("B11 RAW 2.5 ms, then 100 ms without disconnecting",
            rawA, PERIOD_FAST_MS, LATENCY_MS,
            PERIOD_SLOW_MS, LATENCY_NONE);

    stageRateChange("B12 FIELD 100 ms, then 10 ms without disconnecting",
            field, PERIOD_SLOW_MS, LATENCY_NONE,
            PERIOD_MID_MS, LATENCY_NONE);

    //// The fast subscriber leaves; the slow one must survive the part being
    //// slowed back down to it.

    banner("B13 RAW 400 Hz + FIELD 10 Hz, then the fast one leaves");

    const bool rawUp   = rawA.start(PERIOD_FAST_MS, LATENCY_MS);
    const bool fieldUp = field.start(PERIOD_SLOW_MS, LATENCY_NONE);

    if (rawUp || fieldUp) {
        observe(STAGE_S / 2U, rawUp ? &rawA.tally() : nullptr,
                fieldUp ? &field.tally() : nullptr);

        if (rawUp) {
            rawA.tally().summary();
            rawA.stop();
        }

        LOG_INFO_WP("     ... the fast subscription is gone\n");

        if (fieldUp) {
            // Counted from after the part has slowed down, as in the
            // rate-change stages.
            OS::Delay(SWITCH_MS);

            field.tally().restart(PERIOD_SLOW_MS);

            observe(STAGE_S / 2U, nullptr, &field.tally());

            field.tally().summary();
            field.stop();
        }
    }

    OS::Delay(BETWEEN_MS);
}

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestSensorMagnetometer_execute()
{
    LOG_INFO("start\n");

    // Held for the whole run, not per stage.
    const AwakeGuard awake(HOLD_AWAKE != 0);

    if (HOLD_AWAKE) {
        LOG_INFO("deep sleep held off for this run\n");
    }

    static DirectTap tap("PART");

    if (!tap.tally().init()) {
        LOG_ERROR("could not create the lock\n");

        return;
    }

    // Phase A first, and before the sensor layer exists.
    if (!phaseDirect(tap)) {
        return;
    }

    Instance::SensorLayer::getInstance().init();

    // Time for the other sensors' threads to come up and park.
    LOG_INFO("letting the rest of the system settle\n");

    OS::Delay(SETTLE_MS);

    static Channel field(SDK::Sensor::Type::MAGNETIC_FIELD, "FIELD");
    static Channel rawA(SDK::Sensor::Type::MAGNETIC_FIELD_RAW, "RAW.A");
    static Channel rawB(SDK::Sensor::Type::MAGNETIC_FIELD_RAW, "RAW.B");

    if (!field.init() || !rawA.init() || !rawB.init()) {
        LOG_ERROR("could not create the locks\n");

        return;
    }

    for (;;) {
        phaseDirectLoaded(tap);

        phaseLayer(field, rawA, rawB);
    }
}
