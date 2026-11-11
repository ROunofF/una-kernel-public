/**
 ******************************************************************************
 * @file    MagTimeBase_test.cpp
 * @brief   Host tests for the magnetometer's time base: intervals from the
 *          part's counter, an anchor on the system clock, a correction that
 *          follows that clock without stepping, and no instant later than
 *          its read. Plain calls, so every run is the same and none of it
 *          waits on a thread or real time.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Magnetometer/MagTimeBase.hpp"

namespace {

constexpr uint32_t kFastUs   = 2500;    // 400 Hz: 64 counts a conversion
constexpr uint32_t kSlowUs   = 10000;   // 100 Hz: 256 counts a conversion
constexpr uint32_t kFastStep = 64;
constexpr uint32_t kSlowStep = 256;

/// How far an interval may sit from the period over a stream's first few
/// edges, while the rate is still read off a millisecond clock: the part's
/// tolerance, which is what the fit's prior lets the rate be off by. It is
/// headroom over the worst seen here, 21 us at 400 Hz and 87 us after a
/// change to 100 Hz, not a bound derived from the steering. How close an
/// interval settles is for the tests of a part converting on its own
/// oscillator.
constexpr double steerUs(uint32_t periodUs)
{
    return static_cast<double>(((static_cast<uint64_t>(periodUs)
                                 * MagTimeBase::skRatePriorPpm) / 1000000U) + 1U);
}

/// A stream as the worker sees it: the part's counter at each read, the clock
/// at the edge that announced it, and the clock at the read.
struct Stream {
    MagTimeBase           base;
    uint32_t              counter = 0;
    uint32_t              clockMs = 1000;
    std::vector<uint64_t> stamps;

    /// A read counts later, after the clock has moved on by stepMs, and takes
    /// lagMs after the edge. A drained sample has no edge of its own.
    uint64_t read(uint32_t counts, uint32_t stepMs, uint32_t periodUs,
                  bool drained = false, uint32_t lagMs = 0)
    {
        counter  = (counter + counts) & 0x00FFFFFFU;
        clockMs += stepMs;

        const uint64_t at = base.stamp(counter, drained ? 0U : clockMs,
                                       clockMs + lagMs, periodUs);
        stamps.push_back(at);

        return at;
    }

    /// 400 Hz conversions on a millisecond clock keeping pace: 3 and 2 ms in
    /// turn after the first.
    void fast(uint32_t howMany)
    {
        for (uint32_t i = 0; i < howMany; i++) {
            const uint32_t step = (i == 0U) ? 0U : (((i % 2U) != 0U) ? 3U : 2U);
            (void) read(kFastStep, step, kFastUs);
        }
    }

    uint64_t interval(size_t i) const
    {
        return stamps[i] - stamps[i - 1];
    }
};

} // namespace

// 64 counts of 39.0625 us per conversion at 400 Hz: 2500 us apart, though the
// edges are stamped on a clock that only counts whole milliseconds.
TEST(MagTimeBase, IntervalsComeFromThePartNotTheClock)
{
    Stream s;
    s.fast(40);

    for (size_t i = 1; i < s.stamps.size(); i++) {
        EXPECT_NEAR(static_cast<double>(s.interval(i)), 2500.0, steerUs(kFastUs))
                << "step " << i;
    }
}

// The 24-bit counter wraps about every eleven minutes; downstream would read
// a backwards instant as a new stream and a forward jump as a stall.
TEST(MagTimeBase, TheCounterWrappingIsNotVisible)
{
    Stream s;
    s.counter = 0x00FFFFFFU - (kFastStep * 3U);
    s.fast(12);

    for (size_t i = 1; i < s.stamps.size(); i++) {
        EXPECT_NEAR(static_cast<double>(s.interval(i)), 2500.0, steerUs(kFastUs))
                << "step " << i << " across the wrap";
    }
}

// A counter that never moves and a clock that does not either: without a
// guard every instant would repeat.
TEST(MagTimeBase, InstantsNeverRepeatOrGoBackwards)
{
    Stream s;

    for (uint32_t i = 0; i < 6; i++) {
        (void) s.read(0, 0, kFastUs);
    }

    for (size_t i = 1; i < s.stamps.size(); i++) {
        EXPECT_GT(s.stamps[i], s.stamps[i - 1]) << "step " << i;
    }
}

// The first sample lands a millisecond below the one its edge was seen in, and
// so does the first after a reset, wherever the old stream had got to.
TEST(MagTimeBase, AResetAnchorsAfreshOnTheNextEdge)
{
    Stream s;

    EXPECT_EQ(s.read(kFastStep, 0, kFastUs), 999000u);

    s.fast(4);
    s.base.reset();
    s.clockMs = 5000;

    EXPECT_EQ(s.read(kFastStep, 0, kFastUs), 4999000u);
}

// A stream up when the millisecond clock wraps has to wrap with it, or every
// instant sits a whole epoch away from every other sensor's.
TEST(MagTimeBase, TheSystemClockWrappingReanchorsTheStream)
{
    Stream s;

    // The last whole second of the firmware clock.
    s.clockMs = 4194303000U;
    s.fast(4);

    const uint64_t beforeWrap = s.stamps.back();

    s.clockMs = 10;
    s.stamps.clear();

    // After the wrap the clock keeps pace with the conversions, 3 and 2 ms.
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t step = (i == 0U) ? 0U : (((i % 2U) != 0U) ? 3U : 2U);
        (void) s.read(kFastStep, step, kFastUs);
    }

    EXPECT_EQ(s.stamps[0], 9000u)
            << "the stream did not re-anchor when the clock wrapped";

    for (size_t i = 0; i < s.stamps.size(); i++) {
        EXPECT_LT(s.stamps[i], beforeWrap)
                << "sample " << i << " is still counted in the old epoch";
    }
}

// Time counted purely from the part walks away from the system time other
// sensors are stamped on, and the correction has to follow. The drift chosen
// sits inside what the correction may move per sample.
TEST(MagTimeBase, TheCorrectionFollowsAClockThatDrifts)
{
    Stream s;

    // The part reports a period slightly short of the one the clock measures.
    for (uint32_t i = 0; i < 400; i++) {
        (void) s.read(254, 10, kSlowUs);
    }

    const int64_t behindUs = (static_cast<int64_t>(s.clockMs) * 1000)
                           - static_cast<int64_t>(s.stamps.back());

    EXPECT_LT(behindUs, 12000)
            << "the published time fell " << behindUs
            << " us behind the clock - the correction is not following it";

    EXPECT_GT(behindUs, -12000) << "the correction ran past the clock";
}

// A clock running a tenth fast, then a tenth slow, asks for more than the
// correction may give: each interval then moves by exactly its ceiling, a
// thirty-second of the period plus one, and never by more, in either direction.
TEST(MagTimeBase, TheCorrectionMovesNoFasterThanItsCeiling)
{
    constexpr uint64_t kCeilingUs =
            (kSlowUs / MagTimeBase::skSlewCeilingDivisor) + 1U;

    Stream s;

    for (uint32_t i = 0; i < 100; i++) {
        (void) s.read(kSlowStep, 11, kSlowUs);
    }

    uint64_t longest = 0;
    uint32_t atLimit = 0;

    for (size_t i = 1; i < s.stamps.size(); i++) {
        longest  = (s.interval(i) > longest) ? s.interval(i) : longest;
        atLimit += (s.interval(i) == (kSlowUs + kCeilingUs)) ? 1U : 0U;
    }

    EXPECT_LE(longest, kSlowUs + kCeilingUs) << "the correction stepped forward";
    EXPECT_GT(atLimit, 0u) << "a clock a tenth fast never took it to its ceiling";

    // The last instant stays, so the next interval spans the change of pace.
    s.stamps.erase(s.stamps.begin(), s.stamps.end() - 1);

    // Read long after, so the read is not what holds the instants back.
    for (uint32_t i = 0; i < 200; i++) {
        (void) s.read(kSlowStep, 9, kSlowUs, false, 60000);
    }

    uint64_t shortest = 2U * kSlowUs;
    atLimit = 0;

    for (size_t i = 1; i < s.stamps.size(); i++) {
        shortest = (s.interval(i) < shortest) ? s.interval(i) : shortest;
        atLimit += (s.interval(i) == (kSlowUs - kCeilingUs)) ? 1U : 0U;
    }

    EXPECT_GE(shortest, kSlowUs - kCeilingUs) << "the correction stepped back";
    EXPECT_GT(atLimit, 0u) << "a clock a tenth slow never took it to its ceiling";
}

// The line is latched, so conversions completing before a late read raise no
// edges: the edge belongs to the first, the sample to the last. Steering by
// that pair would take the whole delay for drift.
TEST(MagTimeBase, ALateReadDoesNotPullTheCorrection)
{
    Stream s;

    for (uint32_t i = 0; i < 8; i++) {
        (void) s.read(kSlowStep, 10, kSlowUs);
    }

    const uint64_t before = s.stamps.back();
    const uint64_t late   = s.read(kSlowStep * 7U, 10, kSlowUs, false, 60);

    EXPECT_NEAR(static_cast<double>(late - before), 70000.0, 2.0)
            << "the late read pulled the correction";
}

// A sample drained behind another arrives when the bus finished, which says
// nothing about the clocks: it is counted, never steered by. The next sample
// with an edge of its own shows the same clock would have moved it.
TEST(MagTimeBase, ADrainedSampleDoesNotSteer)
{
    constexpr uint64_t kCeilingUs =
            (kSlowUs / MagTimeBase::skSlewCeilingDivisor) + 1U;

    Stream s;

    for (uint32_t i = 0; i < 10; i++) {
        (void) s.read(kSlowStep, 10, kSlowUs);
    }

    // The clock races ahead while only drained samples arrive.
    for (uint32_t i = 0; i < 5; i++) {
        (void) s.read(kSlowStep, 60, kSlowUs, true);
        EXPECT_EQ(s.interval(s.stamps.size() - 1), kSlowUs)
                << "drained sample " << i << " steered the correction";
    }

    (void) s.read(kSlowStep, 10, kSlowUs);
    EXPECT_EQ(s.interval(s.stamps.size() - 1), kSlowUs + kCeilingUs)
            << "the clock was not far enough ahead to show anything";
}

// A rung change does not restart the part, only the counts per conversion;
// the time base reads steps, not absolutes, so it continues unbroken.
TEST(MagTimeBase, ARateChangeMidStreamKeepsTheBase)
{
    Stream s;
    s.fast(8);

    const uint64_t before = s.stamps.back();
    s.stamps.clear();

    for (uint32_t i = 0; i < 8; i++) {
        (void) s.read(kSlowStep, 10, kSlowUs);
    }

    EXPECT_GT(s.stamps[0], before) << "the rung change moved time backwards";

    for (size_t i = 1; i < s.stamps.size(); i++) {
        EXPECT_NEAR(static_cast<double>(s.interval(i)), 10000.0, steerUs(kSlowUs))
                << "step " << i << " after the rung change";
    }
}

namespace {

/// A part converting on its own oscillator, which runs ppm fast or slow of
/// the system clock, seen through a millisecond clock: each edge is handled
/// 20-200 us after its conversion and the sample read 300 us after that.
struct Part {
    MagTimeBase base;
    uint32_t    periodUs;
    uint32_t    periodCounts;
    int32_t     ppm;

    uint32_t counter = 0;
    int64_t  trueNs  = 1000LL * 1000LL * 1000LL * 1000LL;   // 1000 s of uptime
    uint32_t index   = 0;

    uint64_t stamp  = 0;
    int64_t  trueUs = 0;
    uint32_t readMs = 0;

    Part(uint32_t period, uint32_t counts, int32_t offPpm)
        : periodUs(period), periodCounts(counts), ppm(offPpm)
    {
    }

    /// One conversion: a period of the part's own time.
    void next(bool drained = false)
    {
        trueNs += (static_cast<int64_t>(periodUs) * 1000LL * 1000000LL)
                / (1000000LL + ppm);
        counter = (counter + periodCounts) & 0x00FFFFFFU;

        const int64_t latUs  = 20 + ((static_cast<int64_t>(index) * 97) % 181);
        const int64_t edgeUs = (trueNs / 1000) + latUs;

        trueUs = trueNs / 1000;
        readMs = static_cast<uint32_t>((edgeUs + 300) / 1000);
        stamp  = base.stamp(counter,
                            drained ? 0U : static_cast<uint32_t>(edgeUs / 1000),
                            readMs, periodUs);
        index++;
    }

    /// The published interval less the true one, after one more conversion.
    int64_t nextOff(bool drained = false)
    {
        const uint64_t lastStamp = stamp;
        const int64_t  lastTrue  = trueUs;

        next(drained);

        return static_cast<int64_t>(stamp - lastStamp) - (trueUs - lastTrue);
    }
};

struct Case {
    uint32_t periodUs;
    uint32_t counts;
    uint32_t samples;
};

/// 400, 100 and 1.5625 Hz; the slowest takes longest in samples to settle.
constexpr Case kCases[] = {
    {   2500U,    64U, 4000U },
    {  10000U,   256U, 3000U },
    { 640000U, 16384U, 2000U },
};

/// The datasheet allows the oscillator 2% either way; the watch measured runs
/// about 4400 ppm fast, between the two.
constexpr int32_t kOffsets[] = { 2000, -2000, 20000, -20000 };

/// How far an interval may sit from the true one once settled.
constexpr int64_t kSettledUs = 30;

} // namespace

// A consumer compares a sample's instant with the clock it reads afterwards,
// so none may be later than its read: from the first sample, at any rate, and
// on a part further off than the correction can follow.
TEST(MagTimeBase, NoInstantIsLaterThanItsRead)
{
    for (const Case& c : kCases) {
        for (int32_t ppm : { 2000, -2000, 20000, -20000, 100000 }) {
            Part p(c.periodUs, c.counts, ppm);

            for (uint32_t i = 0; i < c.samples; i++) {
                p.next();

                ASSERT_LE(p.stamp, static_cast<uint64_t>(p.readMs) * 1000U)
                        << "period " << c.periodUs << " us, " << ppm
                        << " ppm, sample " << i;
            }
        }
    }
}

// A part whose counter runs off the system clock settles a little behind the
// true instant, whatever the rate, and then keeps the true intervals: held
// under the read by its own steering, not by the bound, which would cut the
// intervals to the millisecond.
TEST(MagTimeBase, APartOffTheClockSettlesBehindTheTrueInstant)
{
    for (const Case& c : kCases) {
        for (int32_t ppm : kOffsets) {
            Part p(c.periodUs, c.counts, ppm);

            for (uint32_t i = 0; i < c.samples / 2U; i++) {
                p.next();
            }

            for (uint32_t i = 0; i < c.samples / 2U; i++) {
                const int64_t off    = p.nextOff();
                const int64_t behind = p.trueUs - static_cast<int64_t>(p.stamp);

                ASSERT_GE(behind, 0) << "period " << c.periodUs << " us, "
                                     << ppm << " ppm, sample " << i;
                ASSERT_LE(behind, 2000) << "period " << c.periodUs << " us, "
                                        << ppm << " ppm, sample " << i;
                ASSERT_LE(std::llabs(off), kSettledUs)
                        << "period " << c.periodUs << " us, " << ppm
                        << " ppm, sample " << i;
            }
        }
    }
}

// Samples drained behind another take the known rate but no steering, and
// the stream still settles.
TEST(MagTimeBase, DrainedSamplesStillSettle)
{
    Part p(kSlowUs, kSlowStep, 20000);

    for (uint32_t i = 0; i < 3000U; i++) {
        p.next((i % 4U) == 3U);
    }

    for (uint32_t i = 0; i < 400U; i++) {
        ASSERT_LE(std::llabs(p.nextOff((i % 4U) == 3U)), kSettledUs)
                << "sample " << i;
    }
}

// A stop pauses the counter, not the oscillator: the rate learnt carries into
// the next stream, whose intervals are true from its start.
TEST(MagTimeBase, ARestartKeepsTheRate)
{
    Part p(kSlowUs, kSlowStep, 20000);

    for (uint32_t i = 0; i < 3000U; i++) {
        p.next();
    }

    p.base.reset();
    p.trueNs += 5LL * 1000LL * 1000LL * 1000LL;
    p.next();

    for (uint32_t i = 0; i < 64U; i++) {
        ASSERT_LE(std::llabs(p.nextOff()), kSettledUs)
                << "sample " << i << " after the restart";
    }
}

// A cold start - nothing known of the part's oscillator yet, as on the first
// stream after boot - neither runs ahead of the conversions nor bends an
// interval past the ceiling while the rate is found, at any rate and at the
// ends of the part's tolerance.
TEST(MagTimeBase, AColdStartNeverRunsAheadOfTheConversions)
{
    constexpr Case kCold[] = {
        {  2500U,   64U, 4000U },
        { 10000U,  256U, 1500U },
        { 80000U, 2048U,  400U },
    };

    for (const Case& c : kCold) {
        for (int32_t ppm : { 4400, -4400, 20000, -20000 }) {
            Part p(c.periodUs, c.counts, ppm);
            p.next();

            const int64_t ceilingUs = static_cast<int64_t>(c.periodUs / 32U) + 1;

            for (uint32_t i = 0; i < c.samples; i++) {
                const int64_t off    = p.nextOff();
                const int64_t behind = p.trueUs - static_cast<int64_t>(p.stamp);

                ASSERT_GE(behind, 0) << "period " << c.periodUs << " us, " << ppm
                                     << " ppm, sample " << i;
                ASSERT_LE(std::llabs(off), ceilingUs + 1)
                        << "period " << c.periodUs << " us, " << ppm
                        << " ppm, sample " << i;
            }
        }
    }
}

// The rate carried into a stream is a starting point, not a verdict: a part
// that has warmed or cooled since runs at another rate, and the new stream
// must settle on that one, not somewhere between the two.
TEST(MagTimeBase, ARestartFollowsAPartThatChangedItsRate)
{
    for (int32_t change : { 4000, -4000 }) {
        Part p(kSlowUs, kSlowStep, -change / 2);

        for (uint32_t i = 0; i < 3000U; i++) {
            p.next();
        }

        p.base.reset();
        p.ppm    += change;
        p.trueNs += 5LL * 1000LL * 1000LL * 1000LL;

        for (uint32_t i = 0; i < 3000U; i++) {
            p.next();
        }

        for (uint32_t i = 0; i < 400U; i++) {
            const int64_t off    = p.nextOff();
            const int64_t behind = p.trueUs - static_cast<int64_t>(p.stamp);

            ASSERT_GE(behind, 0) << change << " ppm change, sample " << i;
            ASSERT_LE(behind, 2000) << change << " ppm change, sample " << i;
            ASSERT_LE(std::llabs(off), kSettledUs) << change << " ppm change, sample " << i;
        }
    }
}

// Every instant published follows the last one and is no later than its read.
// Where the read leaves no such instant - two reads in one millisecond with
// the stream ahead of them, or a clock that went back - the sample is
// withheld rather than given a repeated or earlier instant, and the stream
// carries on once the clock allows.
TEST(MagTimeBase, AnInstantThatCannotFollowTheLastIsWithheld)
{
    Stream s;
    s.fast(8);

    // Four conversions counted while the clock stands still: the first is held
    // to the read, the next has no room left in that millisecond.
    const uint64_t held = s.read(kFastStep * 4U, 0, kFastUs);
    EXPECT_EQ(held, static_cast<uint64_t>(s.clockMs) * 1000U);
    EXPECT_EQ(s.read(kFastStep, 0, kFastUs), MagTimeBase::skWithheld)
            << "a second read in the same millisecond was given an instant";

    // The clock steps back by 5 ms, less than a jump.
    s.clockMs -= 5U;
    EXPECT_EQ(s.read(kFastStep, 0, kFastUs), MagTimeBase::skWithheld)
            << "a read on a clock that went back was given an instant";

    // Once the clock has moved on, instants resume.
    for (uint32_t i = 0; i < 8; i++) {
        (void) s.read(kFastStep, 3, kFastUs);
    }

    uint64_t last = 0;
    for (size_t i = 0; i < s.stamps.size(); i++) {
        if (s.stamps[i] == MagTimeBase::skWithheld) {
            continue;
        }
        EXPECT_GT(s.stamps[i], last) << "instant " << i << " does not follow the last";
        last = s.stamps[i];
    }
    EXPECT_NE(s.stamps.back(), MagTimeBase::skWithheld) << "the stream did not resume";
    EXPECT_LE(s.stamps.back(), static_cast<uint64_t>(s.clockMs) * 1000U);
}
