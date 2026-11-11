/**
 * Host unit tests for Sensor::SampleRateAdapter - the per-listener delivery
 * decimator behind Sensor::DataQueue::pushData().
 *
 * All times are MICROseconds: DataQueue builds the adapter with the period in
 * microseconds and feeds it the frame timestamp widened to microseconds.
 *
 * "real" steps below are what the timestamps say; "nominal" is what the source
 * reports it was set to. The two differ by the chip's oscillator error, which
 * the decision must not depend on.
 */

#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "SDK/SensorLayer/SensorData.hpp"
#include "SDK/SensorLayer/SensorDataView.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"

namespace {

constexpr uint64_t kMs = 1000;          // one millisecond, in microseconds
constexpr uint64_t kSec = 1000 * kMs;

struct Stream
{
    uint64_t           startUs   = 10 * kSec;
    double             realUs    = 20000.0; ///< step the timestamps carry
    uint32_t           nominalUs = 20000;   ///< step the source reports
    int                count     = 3000;
    std::set<int>      lost;                ///< indices of frames never seen
    int                jitterUs  = 0;       ///< +/- uniform, per frame
};

// Feed a stream and return the timestamps delivered.
std::vector<uint64_t> feed(Sensor::SampleRateAdapter& sra, const Stream& s)
{
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> jitter(-s.jitterUs, s.jitterUs);
    std::vector<uint64_t> out;

    for (int i = 0; i < s.count; ++i) {
        if (s.lost.count(i) != 0) {
            continue;
        }

        const int64_t j = (s.jitterUs != 0) ? jitter(rng) : 0;
        const uint64_t ts = s.startUs + static_cast<uint64_t>(i * s.realUs + 0.5) + j;

        if (sra.shouldEmit(ts, s.nominalUs)) {
            out.push_back(ts);
        }
    }

    return out;
}

std::vector<uint64_t> intervals(const std::vector<uint64_t>& ts)
{
    std::vector<uint64_t> gaps;

    for (size_t i = 1; i < ts.size(); ++i) {
        gaps.push_back(ts[i] - ts[i - 1]);
    }

    return gaps;
}

} // namespace

// ---------------------------------------------------------------------------
// The case the issue is about
// ---------------------------------------------------------------------------

// A 40 ms listener on a nominal 50 Hz source gets every second sample however
// the chip's oscillator runs. The old timestamp rule doubled the rate at
// 20.001 ms and above.
TEST(SampleRateAdapter, DecimationDoesNotDependOnTheChipsOscillator)
{
    for (double realUs : {19900.0, 20000.0, 20001.0, 20100.0}) {
        Sensor::SampleRateAdapter sra(40 * kMs);
        Stream s;
        s.realUs = realUs;

        const auto out = feed(sra, s);

        EXPECT_EQ(out.size(), 1500u) << "real step " << realUs << " us";

        for (uint64_t gap : intervals(out)) {
            EXPECT_NEAR(static_cast<double>(gap), 2 * realUs, 1.0)
                << "uneven spacing at real step " << realUs << " us";
        }
    }
}

TEST(SampleRateAdapter, FastStreamIsDecimatedByTheWholeRatio)
{
    // 1600 Hz (625 us) to 25 Hz: N = 64, on a part 0.16 % slow.
    Sensor::SampleRateAdapter sra(40 * kMs);
    Stream s;
    s.realUs = 626.0;
    s.nominalUs = 625;
    s.count = 64 * 100;

    EXPECT_EQ(feed(sra, s).size(), 100u);
}

// The count holds against a few per cent of oscillator error either way, even
// at the largest divisor, where a time threshold would slip first.
TEST(SampleRateAdapter, CountSurvivesOscillatorErrorAtLargeDivisor)
{
    for (double realUs : {625.0 * 0.98, 625.0 * 1.02}) {
        Sensor::SampleRateAdapter sra(40 * kMs);
        Stream s;
        s.realUs = realUs;
        s.nominalUs = 625;
        s.count = 64 * 100;

        EXPECT_EQ(feed(sra, s).size(), 100u) << "real step " << realUs << " us";
    }
}

TEST(SampleRateAdapter, JitterDoesNotBreakTheCount)
{
    Sensor::SampleRateAdapter sra(40 * kMs);
    Stream s;
    s.realUs = 626.0;
    s.nominalUs = 625;
    s.count = 64 * 100;
    s.jitterUs = 150;

    EXPECT_EQ(feed(sra, s).size(), 100u);
}

// ---------------------------------------------------------------------------
// Rates that do not divide
// ---------------------------------------------------------------------------

TEST(SampleRateAdapter, PeriodOffTheLadderIsServedNoSlowerThanAsked)
{
    // 33 ms on a 20 ms source: N = 1, every sample.
    Sensor::SampleRateAdapter every(33 * kMs);
    Stream s;
    s.count = 300;

    EXPECT_EQ(feed(every, s).size(), 300u);

    // 70 ms on a 20 ms source: N = 3, 60 ms apart.
    Sensor::SampleRateAdapter third(70 * kMs);

    EXPECT_EQ(feed(third, s).size(), 100u);
}

TEST(SampleRateAdapter, SourceSlowerThanThePeriodPassesWhole)
{
    Sensor::SampleRateAdapter sra(40 * kMs);
    Stream s;
    s.realUs = 60000.0;
    s.nominalUs = 60000;
    s.count = 100;

    EXPECT_EQ(feed(sra, s).size(), 100u);
}

TEST(SampleRateAdapter, SourceWithoutAStepPassesWhole)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);
    Stream s;
    s.nominalUs = 0;
    s.count = 300;

    EXPECT_EQ(feed(sra, s).size(), 300u);

    // Including samples that share one timestamp.
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(sra.shouldEmit(100 * kSec, 0));
    }
}

// ---------------------------------------------------------------------------
// Restarts
// ---------------------------------------------------------------------------

TEST(SampleRateAdapter, FirstSampleIsDelivered)
{
    // Also within the first half-period after boot, which the old rule dropped.
    Sensor::SampleRateAdapter atBoot(1000 * kMs);
    EXPECT_TRUE(atBoot.shouldEmit(400 * kMs, 20000));

    Sensor::SampleRateAdapter atRuntime(1000 * kMs);
    EXPECT_TRUE(atRuntime.shouldEmit(10 * kSec, 20000));
}

TEST(SampleRateAdapter, AfterAGapTheFirstSampleIsDeliveredAndTheCountRestarts)
{
    // N = 2; frames 101..107 never arrive (a flushed FIFO).
    Sensor::SampleRateAdapter sra(40 * kMs);
    Stream s;
    s.realUs = 20100.0;
    s.count = 300;
    s.lost = {101, 102, 103, 104, 105, 106, 107};

    const auto out = feed(sra, s);
    const uint64_t firstAfter = s.startUs + static_cast<uint64_t>(108 * s.realUs + 0.5);

    ASSERT_NE(std::find(out.begin(), out.end(), firstAfter), out.end())
        << "the first sample after the gap was not delivered";

    // Even spacing on both sides of the gap; the gap itself is the only
    // longer interval.
    int longer = 0;

    for (uint64_t gap : intervals(out)) {
        if (gap > 40200 + 1) {
            ++longer;
        } else {
            EXPECT_NEAR(static_cast<double>(gap), 40200.0, 1.0);
        }
    }

    EXPECT_EQ(longer, 1);
}

// Losing fewer frames than make a gap keeps the count; the delivery that
// straddles the loss is at most one step later than the usual spacing. That
// spacing is N steps as the stamps run - 40.2 ms here, not the 40 ms asked for.
TEST(SampleRateAdapter, NoDeliveryIsMoreThanOneStepLate)
{
    for (int lostAt = 100; lostAt < 104; ++lostAt) {
        Sensor::SampleRateAdapter sra(40 * kMs);
        Stream s;
        s.realUs = 20100.0;
        s.count = 300;
        s.lost = {lostAt};

        for (uint64_t gap : intervals(feed(sra, s))) {
            EXPECT_LE(gap, 3 * 20100 + 1) << "frame " << lostAt << " lost";
        }
    }
}

// ---------------------------------------------------------------------------
// A change of the source step
// ---------------------------------------------------------------------------

// A faster listener joins: the source speeds up under a listener that had
// every sample. Its next one is still a period after its last.
TEST(SampleRateAdapter, ASourceSpeedingUpKeepsTheSpacing)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 1000000));

    // The first fast sample comes 600 ms on, the rest 100 ms apart.
    for (uint64_t ms = 600; ms < 1000; ms += 100) {
        EXPECT_FALSE(sra.shouldEmit(t0 + ms * kMs, 100000)) << ms << " ms";
    }
    EXPECT_TRUE(sra.shouldEmit(t0 + 1000 * kMs, 100000));

    // And every tenth from there.
    for (uint64_t ms = 1100; ms < 2000; ms += 100) {
        EXPECT_FALSE(sra.shouldEmit(t0 + ms * kMs, 100000)) << ms << " ms";
    }
    EXPECT_TRUE(sra.shouldEmit(t0 + 2000 * kMs, 100000));
}

// The fast stream starts anywhere in the slow period: the sample handed over
// is the one nearest a period after the last, within half a new step.
TEST(SampleRateAdapter, ASourceSpeedingUpOffTheGridIsWithinHalfAStep)
{
    for (uint64_t firstMs : {37u, 149u, 537u, 560u, 951u, 999u}) {
        Sensor::SampleRateAdapter sra(1000 * kMs);

        const uint64_t t0 = 10 * kSec;
        ASSERT_TRUE(sra.shouldEmit(t0, 1000000));

        uint64_t passedMs = 0;

        for (uint64_t ms = firstMs; (ms < 3000) && (passedMs == 0); ms += 100) {
            if (sra.shouldEmit(t0 + ms * kMs, 100000)) {
                passedMs = ms;
            }
        }

        EXPECT_GE(passedMs, 950u) << "first fast sample at " << firstMs << " ms";
        EXPECT_LT(passedMs, 1050u) << "first fast sample at " << firstMs << " ms";
    }
}

// The first sample at the new step is itself a period late or more: it goes.
TEST(SampleRateAdapter, ASourceSpeedingUpLateDeliversAtOnce)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 40000));

    EXPECT_TRUE(sra.shouldEmit(t0 + 50 * kMs, 10000));

    for (int i = 1; i < 4; ++i) {
        EXPECT_FALSE(sra.shouldEmit(t0 + (50 + i * 10) * kMs, 10000)) << "sample " << i;
    }
    EXPECT_TRUE(sra.shouldEmit(t0 + 90 * kMs, 10000));
}

// The faster listener leaves: the source slows down under a listener that
// was counting its samples, and the slow grid holds the fast one's instants.
TEST(SampleRateAdapter, ASourceSlowingDownKeepsTheSpacing)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 100000));

    for (uint64_t ms = 100; ms <= 400; ms += 100) {
        EXPECT_FALSE(sra.shouldEmit(t0 + ms * kMs, 100000)) << ms << " ms";
    }

    // The slow grid holds the fast one's instants: the next sample is the
    // one this listener was waiting for.
    EXPECT_TRUE(sra.shouldEmit(t0 + 1000 * kMs, 1000000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 2000 * kMs, 1000000));
}

// A source that starts its slow step from wherever it was: a sample under
// half a period after the last one handed over waits for the next, so no step
// is shorter than half a period and none reaches two.
TEST(SampleRateAdapter, ASourceSlowingDownOffTheGridStaysUnderTwoPeriods)
{
    for (uint64_t firstMs : {120u, 480u, 520u, 1000u, 1400u}) {
        Sensor::SampleRateAdapter sra(1000 * kMs);

        const uint64_t t0 = 10 * kSec;
        ASSERT_TRUE(sra.shouldEmit(t0, 100000));
        ASSERT_FALSE(sra.shouldEmit(t0 + 100 * kMs, 100000));

        const bool atOnce = sra.shouldEmit(t0 + firstMs * kMs, 1000000);

        EXPECT_EQ(atOnce, firstMs >= 500u) << "first slow sample at " << firstMs << " ms";

        if (!atOnce) {
            EXPECT_TRUE(sra.shouldEmit(t0 + (firstMs + 1000) * kMs, 1000000));
        }
    }
}

// Samples taken at the fast rate can still be on their way when the source
// reports the slow step: they are no more this listener's than they were.
TEST(SampleRateAdapter, FastSamplesReportedAtTheSlowStepAreNotDelivered)
{
    Sensor::SampleRateAdapter sra(320 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 10000));

    for (uint64_t ms = 10; ms <= 80; ms += 10) {
        ASSERT_FALSE(sra.shouldEmit(t0 + ms * kMs, 10000));
    }

    for (uint64_t ms = 90; ms <= 120; ms += 10) {
        EXPECT_FALSE(sra.shouldEmit(t0 + ms * kMs, 320000)) << ms << " ms";
    }

    // The first sample of the slow stream, and its second.
    EXPECT_TRUE(sra.shouldEmit(t0 + 400 * kMs, 320000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 720 * kMs, 320000));
}

// The stamps decide only until the first sample handed over after the change.
// From there the count is by the nominal step again, whatever the part's own
// clock makes of it: 2 % fast here, at a divisor of 64, where stamps would
// have every sixty-fifth. A fast part keeps clear of the gap rule, which a
// slow one at this divisor would meet at the same sample as the count.
TEST(SampleRateAdapter, AfterAChangeOfStepTheCountIsByTheNominalStepAgain)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    const uint64_t t0 = 10 * kSec;
    ASSERT_TRUE(sra.shouldEmit(t0, 20000));

    std::vector<int> passed;

    for (int i = 1; i <= 6400; ++i) {
        const uint64_t ts = t0 + static_cast<uint64_t>(i * 612.5 + 0.5);

        if (sra.shouldEmit(ts, 625)) {
            passed.push_back(i);
        }
    }

    ASSERT_GE(passed.size(), 3u);

    for (size_t k = 1; k < passed.size(); ++k) {
        EXPECT_EQ(passed[k] - passed[k - 1], 64) << "delivery " << k;
    }
}

// The stamps of a part on a grid of its own are a millisecond or two off it.
TEST(SampleRateAdapter, AChangeOfStepToleratesStampJitter)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 1000000));

    EXPECT_FALSE(sra.shouldEmit(t0 + 602 * kMs, 100000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 699 * kMs, 100000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 801 * kMs, 100000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 900 * kMs, 100000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 998 * kMs, 100000));
}

// A period that is not a whole multiple of the new step is counted as it is
// on a steady stream: N = 3 of a 300 ms step for a second.
TEST(SampleRateAdapter, AChangeToAStepThatDoesNotDivideThePeriod)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 1000000));

    EXPECT_FALSE(sra.shouldEmit(t0 + 300 * kMs, 300000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 600 * kMs, 300000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 900 * kMs, 300000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 1200 * kMs, 300000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 1500 * kMs, 300000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 1800 * kMs, 300000));
}

// The count across a change is taken from the stamps once; the gap rule then
// runs on the new step as on any stream.
TEST(SampleRateAdapter, AGapAfterAChangeOfStepStillRestartsTheCount)
{
    Sensor::SampleRateAdapter sra(1000 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 1000000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 300 * kMs, 100000));

    // Nothing for 1.2 s, then the stream is back.
    EXPECT_TRUE(sra.shouldEmit(t0 + 1500 * kMs, 100000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 1600 * kMs, 100000));
}

TEST(SampleRateAdapter, StepReportedLaterRestartsTheCount)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    EXPECT_TRUE(sra.shouldEmit(10 * kSec, 0));
    EXPECT_TRUE(sra.shouldEmit(10 * kSec + 20000, 20000));
    EXPECT_FALSE(sra.shouldEmit(10 * kSec + 40000, 20000));
    EXPECT_TRUE(sra.shouldEmit(10 * kSec + 60000, 20000));

    // Lost and found again: the same step counts as a change after a gap in
    // the reporting, so the first sample back is delivered.
    EXPECT_TRUE(sra.shouldEmit(10 * kSec + 80000, 0));
    EXPECT_TRUE(sra.shouldEmit(10 * kSec + 100000, 20000));
    EXPECT_FALSE(sra.shouldEmit(10 * kSec + 120000, 20000));
}

TEST(SampleRateAdapter, TimestampGoingBackwardsRestartsTheCount)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    EXPECT_TRUE(sra.shouldEmit(10 * kSec, 20000));
    EXPECT_TRUE(sra.shouldEmit(5 * kSec, 20000));
    EXPECT_FALSE(sra.shouldEmit(5 * kSec + 20000, 20000));
    EXPECT_TRUE(sra.shouldEmit(5 * kSec + 40000, 20000));
}

TEST(SampleRateAdapter, ResetDeliversTheNextSample)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    EXPECT_TRUE(sra.shouldEmit(10 * kSec, 20000));

    sra.reset();

    EXPECT_TRUE(sra.shouldEmit(10 * kSec + 20000, 20000));
    EXPECT_FALSE(sra.shouldEmit(10 * kSec + 40000, 20000));
}

TEST(SampleRateAdapter, SetExpectedPeriodChangesDecimation)
{
    Sensor::SampleRateAdapter sra(40 * kMs);
    Stream s;
    s.count = 300;

    EXPECT_EQ(feed(sra, s).size(), 150u);

    sra.setExpectedPeriod(80 * kMs);
    sra.reset();
    s.startUs = 100 * kSec;

    EXPECT_EQ(feed(sra, s).size(), 75u);
}

// A new period without a reset keeps what was counted: the next sample handed
// over is a new period after the last one.
TEST(SampleRateAdapter, ANewPeriodWithoutAResetKeepsTheCount)
{
    Sensor::SampleRateAdapter sra(40 * kMs);

    const uint64_t t0 = 10 * kSec;
    EXPECT_TRUE(sra.shouldEmit(t0, 20000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 20 * kMs, 20000));

    sra.setExpectedPeriod(80 * kMs);

    EXPECT_FALSE(sra.shouldEmit(t0 + 40 * kMs, 20000));
    EXPECT_FALSE(sra.shouldEmit(t0 + 60 * kMs, 20000));
    EXPECT_TRUE(sra.shouldEmit(t0 + 80 * kMs, 20000));
}

// ---------------------------------------------------------------------------
// Helpers for derived sensors
// ---------------------------------------------------------------------------

TEST(SampleRateAdapter, DivisorAndOutputStep)
{
    EXPECT_EQ(Sensor::SampleRateAdapter::divisor(40 * kMs, 20000), 2u);
    EXPECT_EQ(Sensor::SampleRateAdapter::divisor(40 * kMs, 625), 64u);
    EXPECT_EQ(Sensor::SampleRateAdapter::divisor(33 * kMs, 20000), 1u);
    EXPECT_EQ(Sensor::SampleRateAdapter::divisor(10 * kMs, 20000), 1u);

    EXPECT_EQ(Sensor::SampleRateAdapter::outputStepUs(40 * kMs, 625), 40000u);
    EXPECT_EQ(Sensor::SampleRateAdapter::outputStepUs(70 * kMs, 20000), 60000u);
    EXPECT_EQ(Sensor::SampleRateAdapter::outputStepUs(10 * kMs, 20000), 20000u);
    EXPECT_EQ(Sensor::SampleRateAdapter::outputStepUs(40 * kMs, 0), 0u);

    // A step past 32 bits is reported as none, not wrapped: 5000 s here.
    EXPECT_EQ(Sensor::SampleRateAdapter::outputStepUs(5000 * kSec, 1000 * 1000), 0u);
}

// ---------------------------------------------------------------------------
// Microsecond wrap
// ---------------------------------------------------------------------------

// The timestamp fields must stay 64-bit: past 2^32 us (~71.6 min) a narrower
// comparison reads every sample as "after a gap" or "backwards" and restarts
// the count on each one.
TEST(SampleRateAdapter, DecimationSurvivesPastTheMicrosecondWrap)
{
    constexpr uint64_t kWrap = static_cast<uint64_t>(UINT32_MAX) + 1; // ~4295 s

    for (uint64_t uptimeUs : {60 * kSec, 4000 * kSec, kWrap - 30 * kSec,
                              kWrap + 5 * kSec, 20000 * kSec}) {
        Sensor::SampleRateAdapter sra(1000 * kMs);
        Stream s;
        s.startUs = uptimeUs;
        s.realUs = 333333.0;
        s.nominalUs = 333333;
        s.count = 120;

        EXPECT_EQ(feed(sra, s).size(), 40u)
            << "decimation broke at uptime " << (uptimeUs / kSec) << " s";
    }
}

// The adapter is only as wide as the value it is handed, so pin the conversion
// as well as the comparison. This uses the same DataView helper pushData()
// calls, so it guards the conversion the call site relies on.
TEST(SampleRateAdapter, CallerDerivedTimestampSurvivesTheWrap)
{
    SDK::Sensor::Data frame {};
    Sensor::SampleRateAdapter sra(1000 * kMs);

    // Straddle the ~71.6 min boundary where `mTimeStamp * 1000` overflows
    // 32 bits: 4200 s -> 4400 s of uptime, sampled every 333 ms.
    int delivered = 0;

    for (int i = 0; i < 600; ++i) {
        frame.mTimeStamp   = 4200u * 1000u + static_cast<uint32_t>(i) * 333u;
        frame.mTimeStampUs = 0;

        const uint64_t nowUs =
                SDK::Sensor::DataView(frame, /*fieldCount=*/1).getTimestampUs();

        if (sra.shouldEmit(nowUs, 333000)) {
            ++delivered;
        }
    }

    // One in three. A narrowed conversion goes backwards at the wrap, which
    // restarts the count there and delivers an extra sample.
    EXPECT_EQ(delivered, 200);
}
