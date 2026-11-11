#include <gtest/gtest.h>

#include "SensorLayer/Sensors/Cadence/CadenceEstimator.hpp"
#include "SensorLayer/Sensors/Cadence/CadenceEstimatorConfig.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using Sensor::CadenceEstimator;

namespace
{

constexpr uint64_t kSecondUs = 1'000'000ULL;

// Cumulative step-counter feed: advances one second at a time, accruing
// fractional steps so any rate (in steps per minute) yields an integer,
// monotonic cumulative count, and calls onStepCount + tickUs each second.
struct CounterFeed {
    explicit CounterFeed(uint64_t startUs) : tUs(startUs) {}

    uint64_t tUs;
    uint32_t count     = 0;
    double   remainder = 0.0;
};

void feedCounter(CadenceEstimator& est,
                 CounterFeed&      feed,
                 float             ratePerMin,
                 uint32_t          seconds)
{
    for (uint32_t i = 0; i < seconds; ++i) {
        feed.tUs += kSecondUs;
        feed.remainder += static_cast<double>(ratePerMin) / 60.0;
        const uint32_t whole = static_cast<uint32_t>(feed.remainder);
        feed.count += whole;
        feed.remainder -= whole;
        est.onStepCount(feed.tUs, feed.count);
        est.tickUs(feed.tUs);
    }
}

// One count quantum over the full rate window, in SPM: an integer cumulative
// count sampled at 1 Hz cannot pin the rate tighter than this.
constexpr float kWindowQuantumSpm =
    60.0e6f / static_cast<float>(Sensor::CadenceConfig::kWindowUs);

} // namespace

TEST(CadenceEstimatorTest, AcquiresAfterMinWindow)
{
    CadenceEstimator est;

    const uint64_t t0 = 1'000'000'000ULL;
    est.onStepCount(t0, 0u);
    est.tickUs(t0);
    EXPECT_FALSE(est.getSample().cadenceValid) << "single sample must not acquire";

    // 180 SPM = 3 steps per second.
    for (uint32_t k = 1; k <= 5; ++k) {
        const uint64_t t = t0 + k * kSecondUs;
        est.onStepCount(t, 3u * k);
        est.tickUs(t);

        const uint64_t spanUs = k * kSecondUs;
        if (spanUs < Sensor::CadenceConfig::kMinAcqWindowUs) {
            EXPECT_FALSE(est.getSample().cadenceValid) << "span " << k << " s";
        } else {
            const auto sample = est.getSample();
            EXPECT_TRUE(sample.cadenceValid) << "span " << k << " s";
            EXPECT_NEAR(sample.cadenceSpm, 180.0f, 0.01f);
        }
    }
}

TEST(CadenceEstimatorTest, SteadyHighCadenceNoCollapse)
{
    // The field regression: steady ~170 SPM wrist running must never halve,
    // never go invalid, and must stay pinned to the counter rate.
    CadenceEstimator est;
    CounterFeed      feed(2'000'000'000ULL);

    // Acquisition plus one full window - the tight band below applies once
    // the window has filled (a partial-span window has a coarser quantum).
    feedCounter(est, feed, 170.0f, 9);
    ASSERT_TRUE(est.getSample().cadenceValid);

    float sum = 0.0f;
    int   n   = 0;
    for (uint32_t s = 0; s < 21; ++s) {
        feedCounter(est, feed, 170.0f, 1);
        const auto sample = est.getSample();
        ASSERT_TRUE(sample.cadenceValid) << "cadence lost at second " << (10 + s);
        // An integer count over the window quantises the instantaneous
        // estimate to +/- one window quantum (7.5 SPM at 8 s).
        EXPECT_NEAR(sample.cadenceSpm, 170.0f, kWindowQuantumSpm)
            << "at second " << (10 + s);
        sum += sample.cadenceSpm;
        ++n;
    }

    // The quantisation dithers around the true rate: the mean must be tight.
    EXPECT_NEAR(sum / static_cast<float>(n), 170.0f, 2.5f);
}

TEST(CadenceEstimatorTest, JitteredTimestampsDoNotCollapse)
{
    // Steady count with randomised sub-second sample times: the ms-quantised
    // batching jitter that made the ISI estimator halve must not disturb the
    // windowed rate.
    CadenceEstimator est;

    std::mt19937                           rng(0xC0DECAFEu);
    std::uniform_int_distribution<int32_t> jitterUs(-100'000, 100'000);

    const uint64_t t0 = 3'000'000'000ULL;
    est.onStepCount(t0, 0u);
    est.tickUs(t0);

    // 180 SPM = 3 steps per second, sampled once per second +/- 100 ms.
    for (uint32_t k = 1; k <= 40; ++k) {
        const uint64_t nominalUs = t0 + k * kSecondUs;
        const uint64_t sampleUs =
            nominalUs + static_cast<uint64_t>(100'000 + jitterUs(rng)) - 100'000u;
        est.onStepCount(sampleUs, 3u * k);
        est.tickUs(nominalUs);

        const auto sample = est.getSample();
        if (k >= 4) {
            ASSERT_TRUE(sample.cadenceValid) << "cadence lost at sample " << k;
        }
        if (k >= 8) {
            EXPECT_NEAR(sample.cadenceSpm, 180.0f, 6.0f) << "at sample " << k;
        }
    }
}

TEST(CadenceEstimatorTest, WindowTracksGenuineSlowdown)
{
    CadenceEstimator est;
    CounterFeed      feed(4'000'000'000ULL);

    feedCounter(est, feed, 180.0f, 15);
    ASSERT_TRUE(est.getSample().cadenceValid);
    ASSERT_NEAR(est.getSample().cadenceSpm, 180.0f, 0.01f);

    // Genuine slowdown to 120 SPM: output stays valid throughout and
    // converges once the window has fully turned over.
    for (uint32_t s = 1; s <= 15; ++s) {
        feedCounter(est, feed, 120.0f, 1);
        const auto sample = est.getSample();
        ASSERT_TRUE(sample.cadenceValid) << "cadence lost " << s << " s after slowdown";
        EXPECT_LE(sample.cadenceSpm, 180.5f);
        if (s * kSecondUs > Sensor::CadenceConfig::kWindowUs) {
            EXPECT_NEAR(sample.cadenceSpm, 120.0f, 1.0f)
                << s << " s after slowdown";
        }
    }
}

TEST(CadenceEstimatorTest, StaleGoesInvalid)
{
    CadenceEstimator est;
    CounterFeed      feed(5'000'000'000ULL);

    feedCounter(est, feed, 180.0f, 10);
    ASSERT_TRUE(est.getSample().cadenceValid);

    // Still fresh at exactly the gap bound...
    est.tickUs(feed.tUs + Sensor::CadenceConfig::kMaxStepGapUs);
    EXPECT_TRUE(est.getSample().cadenceValid);

    // ...invalid one microsecond past it.
    est.tickUs(feed.tUs + Sensor::CadenceConfig::kMaxStepGapUs + 1u);
    EXPECT_FALSE(est.getSample().cadenceValid);
}

TEST(CadenceEstimatorTest, CounterResetIgnored)
{
    CadenceEstimator est;
    CounterFeed      feed(6'000'000'000ULL);

    feedCounter(est, feed, 180.0f, 10);
    ASSERT_TRUE(est.getSample().cadenceValid);

    // The cumulative counter drops (chip reset): rebase, no spike, no stale
    // high reading - output degrades to invalid until re-acquired.
    est.onStepCount(feed.tUs + kSecondUs, 5u);
    est.tickUs(feed.tUs + kSecondUs);
    EXPECT_FALSE(est.getSample().cadenceValid);

    // Re-acquires from the rebased count without any artefact.
    CounterFeed rebased(feed.tUs + kSecondUs);
    rebased.count = 5u;
    for (uint32_t s = 1; s <= 4; ++s) {
        feedCounter(est, rebased, 180.0f, 1);
        const auto sample = est.getSample();
        if (sample.cadenceValid) {
            EXPECT_NEAR(sample.cadenceSpm, 180.0f, kWindowQuantumSpm);
        }
    }
    const auto sample = est.getSample();
    ASSERT_TRUE(sample.cadenceValid);
    EXPECT_NEAR(sample.cadenceSpm, 180.0f, 0.01f);
}

TEST(CadenceEstimatorTest, RebasesAfterNonMonotonicTimestamp)
{
    CadenceEstimator est;
    CounterFeed      feed(7'000'000'000ULL);

    feedCounter(est, feed, 180.0f, 10);
    ASSERT_TRUE(est.getSample().cadenceValid);

    // Time going backwards rebases the window (count is monotonic here).
    est.onStepCount(feed.tUs - 5u * kSecondUs, feed.count + 1u);
    est.tickUs(feed.tUs - 5u * kSecondUs);
    EXPECT_FALSE(est.getSample().cadenceValid);
}

TEST(CadenceEstimatorTest, ClampsHighCadenceToMaxAndStaysValid)
{
    // 240 SPM = 4 steps per second: a genuine near-ceiling runner is clamped
    // to the configured maximum and stays valid.
    CadenceEstimator est;
    CounterFeed      feed(8'000'000'000ULL);
    feedCounter(est, feed, 240.0f, 10);

    const auto sample = est.getSample();
    ASSERT_TRUE(sample.cadenceValid);
    EXPECT_FLOAT_EQ(sample.cadenceSpm, Sensor::CadenceConfig::kMaxCadenceSpm);
}

TEST(CadenceEstimatorTest, SlowWalkBelowMinIsInvalid)
{
    // Sustained ~60 SPM (below the running band): rejected as invalid rather
    // than clamped up to kMinCadenceSpm and reported valid.
    CadenceEstimator est;
    CounterFeed      feed(8'500'000'000ULL);
    feedCounter(est, feed, 60.0f, 15);

    const auto sample = est.getSample();
    EXPECT_FALSE(sample.cadenceValid);
    EXPECT_FLOAT_EQ(sample.cadenceSpm, 0.0f);
}

TEST(CadenceEstimatorTest, PauseResumeReacquiresNotDilutedLow)
{
    CadenceEstimator est;
    CounterFeed      feed(8'750'000'000ULL);

    feedCounter(est, feed, 170.0f, 10);
    ASSERT_TRUE(est.getSample().cadenceValid);
    ASSERT_NEAR(est.getSample().cadenceSpm, 170.0f, kWindowQuantumSpm);

    // Gap with no count progress (wearer paused). Once the stale gap bound is
    // exceeded the tick must report invalid (never a diluted valid-low
    // reading); before that it may hold the last good value, but must never
    // dip toward the floor.
    const uint32_t pausedCount = feed.count;
    for (uint32_t g = 1; g <= 4; ++g) {
        est.tickUs(feed.tUs + g * kSecondUs);
        const auto gapSample = est.getSample();
        if (g * kSecondUs > Sensor::CadenceConfig::kMaxStepGapUs) {
            EXPECT_FALSE(gapSample.cadenceValid) << "gap second " << g;
            EXPECT_FLOAT_EQ(gapSample.cadenceSpm, 0.0f);
        } else if (gapSample.cadenceValid) {
            EXPECT_NEAR(gapSample.cadenceSpm, 170.0f, kWindowQuantumSpm)
                << "gap second " << g;
        }
    }

    // Resume 170 SPM after a 4 s pause: the first resumed sample must rebase
    // (gap-rebase in onStepCount) rather than span the pause and clamp to a
    // spurious valid-80.
    CounterFeed resumed(feed.tUs + 4u * kSecondUs);
    resumed.count = pausedCount;
    for (uint32_t s = 1; s <= 3; ++s) {
        feedCounter(est, resumed, 170.0f, 1);
        const auto sample = est.getSample();
        if (sample.cadenceValid) {
            // If it validates during re-acquisition it must be the REAL rate,
            // never the diluted low from spanning the pause. Without the
            // gap-rebase in onStepCount this validates near ~82 SPM and fails
            // here - which is exactly the regression this test pins.
            EXPECT_NEAR(sample.cadenceSpm, 170.0f, kWindowQuantumSpm)
                << "resume second " << s;
        }
    }

    // Fully re-acquired: tracks ~170 again.
    feedCounter(est, resumed, 170.0f, 10);
    const auto sample = est.getSample();
    ASSERT_TRUE(sample.cadenceValid);
    EXPECT_NEAR(sample.cadenceSpm, 170.0f, kWindowQuantumSpm);
}

TEST(CadenceEstimatorTest, SparseSamplesGoInvalid)
{
    // Documents why the sensor layer must supply a dense per-step feed:
    // counter samples spaced further apart than kMaxStepGapUs can never form
    // a valid window - each new sample gap-rebases and the tick immediately
    // sees a stale newest sample.
    CadenceEstimator est;

    const uint64_t stepUs =
        Sensor::CadenceConfig::kMaxStepGapUs + kSecondUs; // > gap bound

    uint64_t t     = 9'800'000'000ULL;
    uint32_t count = 0;
    for (uint32_t k = 0; k < 10; ++k) {
        t += stepUs;
        count += 20u; // 20 steps per interrupt, as the BMI270 watermark
        est.onStepCount(t, count);
        est.tickUs(t);
        EXPECT_FALSE(est.getSample().cadenceValid) << "sparse sample " << k;
    }
}

TEST(CadenceEstimatorTest, CounterWraparoundRebasesNoSpike)
{
    CadenceEstimator est;

    // Cumulative count climbing toward the 32-bit ceiling at 180 SPM.
    const uint64_t t0    = 10'000'000'000ULL;
    uint32_t       count = UINT32_MAX - 30u;
    uint64_t       t     = t0;
    for (uint32_t k = 0; k < 8; ++k) {
        t += kSecondUs;
        count += 3u;
        est.onStepCount(t, count);
        est.tickUs(t);
    }

    // The counter wraps to a low value: treated as a reset (count < last),
    // rebasing the window. No spurious huge delta -> huge SPM reading.
    est.onStepCount(t + kSecondUs, 5u);
    est.tickUs(t + kSecondUs);
    EXPECT_FALSE(est.getSample().cadenceValid);
    EXPECT_FLOAT_EQ(est.getSample().cadenceSpm, 0.0f);

    // Re-acquires cleanly from the wrapped-low base at 180 SPM.
    CounterFeed rebased(t + kSecondUs);
    rebased.count = 5u;
    feedCounter(est, rebased, 180.0f, 6);
    const auto sample = est.getSample();
    ASSERT_TRUE(sample.cadenceValid);
    EXPECT_NEAR(sample.cadenceSpm, 180.0f, kWindowQuantumSpm);
}

TEST(CadenceEstimatorTest, ZeroAndSingleSampleStayInvalidNoDivByZero)
{
    CadenceEstimator est;

    // Tick with an empty ring.
    est.tickUs(9'000'000'000ULL);
    EXPECT_FALSE(est.getSample().cadenceValid);

    // Single sample: zero span.
    const uint64_t t0 = 9'100'000'000ULL;
    est.onStepCount(t0, 100u);
    est.tickUs(t0);
    EXPECT_FALSE(est.getSample().cadenceValid);

    // Second sample at the SAME timestamp coalesces: still zero span.
    est.onStepCount(t0, 102u);
    est.tickUs(t0);
    EXPECT_FALSE(est.getSample().cadenceValid);
    est.tickUs(t0 + kSecondUs);
    EXPECT_FALSE(est.getSample().cadenceValid);

    // Reset and repeat the empty-ring tick.
    est.reset();
    est.tickUs(t0 + 2u * kSecondUs);
    EXPECT_FALSE(est.getSample().cadenceValid);
    EXPECT_FLOAT_EQ(est.getSample().cadenceSpm, 0.0f);
}

TEST(CadenceEstimatorTest, RingEvictionKeepsWindowBounded)
{
    CadenceEstimator est;

    // Flood the ring far beyond its capacity with high-rate samples, then
    // change pace: the estimate must follow the recent window, not history.
    const uint64_t t0 = 9'500'000'000ULL;
    uint32_t       count = 0;
    uint64_t       t     = t0;
    for (uint32_t k = 0; k < 4u * Sensor::CadenceConfig::kMaxSamples; ++k) {
        t += kSecondUs / 4u; // 4 samples per second
        count += 1u;         // 240 SPM, above the output clamp
        est.onStepCount(t, count);
    }
    est.tickUs(t);
    ASSERT_TRUE(est.getSample().cadenceValid);
    EXPECT_FLOAT_EQ(est.getSample().cadenceSpm,
                    Sensor::CadenceConfig::kMaxCadenceSpm);

    // Slow to 120 SPM (2 steps per second): converges after one window.
    for (uint32_t s = 0; s < 10; ++s) {
        t += kSecondUs;
        count += 2u;
        est.onStepCount(t, count);
        est.tickUs(t);
    }
    const auto sample = est.getSample();
    ASSERT_TRUE(sample.cadenceValid);
    EXPECT_NEAR(sample.cadenceSpm, 120.0f, 1.0f);
}

// Replays the on-device wrist-running trace (Tests/Host/kernel/data/
// cadence_debug.csv, columns: elapsed_s,detector_events,counter_total,
// counter_delta,cadence_spm,cadence_valid). The recorded firmware output in
// that trace under-reports every fast window and intermittently drops to
// 0/invalid - the field regression this test pins down.
//
// In every fast window (trailing 10-s counter rate >= 160 SPM) the estimator
// must have acquired (cadenceValid true) and must be within +/-5 SPM of the
// counter-derived rate over its own retained-sample window. The 10-s rate is
// used only as the fast-window gate: the BMI270 publishes the counter in
// ~2-step quanta, so a 10-s reference differs from any window-limited
// estimate by up to ~15 SPM even at perfectly steady cadence.
TEST(CadenceEstimatorTest, WindowedTraceReplayTracksCounterRate)
{
    struct TraceRow {
        uint64_t tUs;
        uint32_t cumulative;
    };

    std::ifstream trace(CADENCE_TRACE_PATH);
    ASSERT_TRUE(trace.is_open()) << "missing trace file: " << CADENCE_TRACE_PATH;

    std::string line;
    ASSERT_TRUE(std::getline(trace, line)); // header

    std::vector<TraceRow> rows;
    uint32_t cumulative = 0;
    while (std::getline(trace, line)) {
        if (line.empty()) {
            continue;
        }
        unsigned long elapsedS       = 0;
        unsigned long detectorEvents = 0;
        unsigned long counterTotal   = 0;
        unsigned long counterDelta   = 0;
        char          sep            = 0;
        std::istringstream fields(line);
        fields >> elapsedS >> sep >> detectorEvents >> sep >> counterTotal
               >> sep >> counterDelta;
        ASSERT_TRUE(static_cast<bool>(fields)) << "unparseable trace row: " << line;
        cumulative += static_cast<uint32_t>(counterDelta);
        rows.push_back({elapsedS * 1'000'000ULL, cumulative});
    }
    ASSERT_GE(rows.size(), 60u);

    CadenceEstimator est;
    std::vector<CadenceEstimator::Sample> out;
    out.reserve(rows.size());
    for (const TraceRow& row : rows) {
        est.onStepCount(row.tUs, row.cumulative);
        est.tickUs(row.tUs);
        out.push_back(est.getSample());
    }

    constexpr size_t kGateRows     = 10;   // 10-s fast-window gate
    constexpr double kFastGateSpm  = 160.0;
    constexpr double kToleranceSpm = 5.0;

    size_t fastRows = 0;
    for (size_t i = kGateRows; i < rows.size(); ++i) {
        const double gateSpanS =
            static_cast<double>(rows[i].tUs - rows[i - kGateRows].tUs) / 1.0e6;
        ASSERT_GT(gateSpanS, 0.0);
        const double gateRateSpm =
            (rows[i].cumulative - rows[i - kGateRows].cumulative) * 60.0 / gateSpanS;
        if (gateRateSpm < kFastGateSpm) {
            continue;
        }
        ++fastRows;

        // Counter-derived rate over the estimator's retained window: oldest
        // retained sample is the first one no older than (now - kWindowUs).
        size_t j = i;
        while (j > 0 &&
               rows[j - 1].tUs + Sensor::CadenceConfig::kWindowUs >= rows[i].tUs) {
            --j;
        }
        const double refSpanS =
            static_cast<double>(rows[i].tUs - rows[j].tUs) / 1.0e6;
        ASSERT_GT(refSpanS, 0.0);
        double refSpm = (rows[i].cumulative - rows[j].cumulative) * 60.0 / refSpanS;
        refSpm = std::min<double>(std::max<double>(refSpm,
                                                   Sensor::CadenceConfig::kMinCadenceSpm),
                                  Sensor::CadenceConfig::kMaxCadenceSpm);

        EXPECT_TRUE(out[i].cadenceValid) << "cadence lost in fast window, row " << i;
        EXPECT_NEAR(out[i].cadenceSpm, refSpm, kToleranceSpm) << "row " << i;
    }
    EXPECT_GE(fastRows, 25u) << "trace no longer exercises fast windows";
}
