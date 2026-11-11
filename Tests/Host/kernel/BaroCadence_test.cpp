/**
 ******************************************************************************
 * @file    BaroCadence_test.cpp
 * @brief   Host tests for the barometer's cadence: the ladder of periods and
 *          how a rate resolves onto it, and the grid of instants a stream
 *          falls due on. Arithmetic on numbers, so no part, RTOS or real time.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "Barometer/BaroCadence.hpp"

namespace {

/// The rate a consumer asking for this period sends.
float rateFor(uint32_t periodMs)
{
    return 1000.0f / static_cast<float>(periodMs);
}

} // namespace

// -----------------------------------------------------------------------------
// The ladder
// -----------------------------------------------------------------------------

// What makes a consumer's rate independent of who else is connected: whichever
// rung the part runs at, every slower rung is a whole number of its steps.
TEST(BaroCadenceLadder, EveryRungIsAWholeMultipleOfEveryFasterOne)
{
    for (size_t slow = 0; slow < BaroCadence::skRungs; slow++) {
        for (size_t fast = 0; fast < slow; fast++) {
            EXPECT_EQ(BaroCadence::skLadderMs[slow] % BaroCadence::skLadderMs[fast], 0u)
                    << BaroCadence::skLadderMs[slow] << " on "
                    << BaroCadence::skLadderMs[fast];
            EXPECT_GT(BaroCadence::skLadderMs[slow], BaroCadence::skLadderMs[fast]);
        }
    }

    EXPECT_TRUE(BaroCadence::isLadderAChain());
}

TEST(BaroCadenceLadder, TheEndsAreTheFirstAndLastRungs)
{
    EXPECT_EQ(BaroCadence::skFastestMs, 100u);
    EXPECT_EQ(BaroCadence::skSlowestMs, 10000u);

    EXPECT_FLOAT_EQ(BaroCadence::maxRateHz(), 10.0f);
    EXPECT_FLOAT_EQ(BaroCadence::minRateHz(), 0.1f);
}

// A period goes down as a rate and has to come back as the same rung, at
// every rung: the rates of the slow ones are not exact in a float.
TEST(BaroCadenceLadder, ARungAskedForAsARateResolvesToItself)
{
    for (uint32_t rungMs : BaroCadence::skLadderMs) {
        EXPECT_EQ(BaroCadence::periodMsFor(rateFor(rungMs)), rungMs)
                << "rungMs=" << rungMs;
    }
}

// The next rate up from a rung's own, in a float, is a period a fraction of
// a microsecond short of the rung. It is still that rung, not the one below
// it.
TEST(BaroCadenceLadder, ARateAHairAboveARungStillGetsThatRung)
{
    for (uint32_t rungMs : BaroCadence::skLadderMs) {
        const float hairAbove = std::nextafter(rateFor(rungMs),
                std::numeric_limits<float>::infinity());

        EXPECT_EQ(BaroCadence::periodMsFor(hairAbove), rungMs)
                << "rungMs=" << rungMs;
    }
}

// Never slower than asked: between two rungs the faster one is taken.
TEST(BaroCadenceLadder, ARateBetweenRungsGetsTheFasterOne)
{
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(150)),   100u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(199)),   100u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(300)),   200u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(999)),   200u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(1001)), 1000u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(4999)), 1000u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(7000)), 5000u);
    EXPECT_EQ(BaroCadence::periodMsFor(rateFor(9999)), 5000u);
}

TEST(BaroCadenceLadder, ARateOffTheLadderGetsTheNearestEnd)
{
    EXPECT_EQ(BaroCadence::periodMsFor(1000.0f), BaroCadence::skFastestMs);
    EXPECT_EQ(BaroCadence::periodMsFor(10.001f), BaroCadence::skFastestMs);
    EXPECT_EQ(BaroCadence::periodMsFor(std::numeric_limits<float>::infinity()),
              BaroCadence::skFastestMs);

    EXPECT_EQ(BaroCadence::periodMsFor(0.05f),  BaroCadence::skSlowestMs);
    EXPECT_EQ(BaroCadence::periodMsFor(0.0f),   BaroCadence::skSlowestMs);
    EXPECT_EQ(BaroCadence::periodMsFor(-1.0f),  BaroCadence::skSlowestMs);
    EXPECT_EQ(BaroCadence::periodMsFor(std::nanf("")), BaroCadence::skSlowestMs);
}

// -----------------------------------------------------------------------------
// The grid
// -----------------------------------------------------------------------------

TEST(BaroCadenceGrid, NothingIsDueBeforeAStart)
{
    BaroCadence cadence;

    EXPECT_FALSE(cadence.isRunning());
    EXPECT_FALSE(cadence.take(0));
    EXPECT_FALSE(cadence.take(1000000));
}

TEST(BaroCadenceGrid, TheFirstMeasurementIsDueAtOnce)
{
    BaroCadence cadence;
    cadence.start(5000, 200);

    EXPECT_EQ(cadence.dueInMs(5000), 0u);
    EXPECT_TRUE(cadence.take(5000));
}

TEST(BaroCadenceGrid, TheNextIsDueAWholePeriodLater)
{
    BaroCadence cadence;
    cadence.start(5000, 200);
    ASSERT_TRUE(cadence.take(5000));

    EXPECT_EQ(cadence.dueInMs(5000), 200u);
    EXPECT_EQ(cadence.dueInMs(5150), 50u);

    EXPECT_FALSE(cadence.take(5199));
    EXPECT_TRUE(cadence.take(5200));
}

// A measurement is taken once: asking again at the same instant gets nothing.
TEST(BaroCadenceGrid, ADeadlineIsTakenOnce)
{
    BaroCadence cadence;
    cadence.start(0, 100);

    EXPECT_TRUE(cadence.take(0));
    EXPECT_FALSE(cadence.take(0));
    EXPECT_FALSE(cadence.take(99));
}

// A wake-up that comes late does not move the grid: the one after it is due
// where it always was.
TEST(BaroCadenceGrid, ALateWakeUpDoesNotPushTheGridBack)
{
    BaroCadence cadence;
    cadence.start(1000, 100);
    ASSERT_TRUE(cadence.take(1000));

    // 30 ms late for the deadline at 1100.
    EXPECT_EQ(cadence.dueInMs(1130), 0u);
    ASSERT_TRUE(cadence.take(1130));

    EXPECT_EQ(cadence.dueInMs(1130), 70u);
    EXPECT_FALSE(cadence.take(1199));
    EXPECT_TRUE(cadence.take(1200));
}

// Just short of a period late still keeps the grid; the next one is close.
TEST(BaroCadenceGrid, AlmostAPeriodLateStillKeepsTheGrid)
{
    BaroCadence cadence;
    cadence.start(1000, 100);
    ASSERT_TRUE(cadence.take(1000));

    ASSERT_TRUE(cadence.take(1199));

    EXPECT_EQ(cadence.dueInMs(1199), 1u);
    EXPECT_TRUE(cadence.take(1200));
}

// A whole period late or more: the grid starts over from now instead of
// answering every deadline that was missed.
TEST(BaroCadenceGrid, AWholePeriodLateStartsTheGridOver)
{
    BaroCadence cadence;
    cadence.start(1000, 100);
    ASSERT_TRUE(cadence.take(1000));

    // The deadline was 1100; this is 450 ms past it.
    ASSERT_TRUE(cadence.take(1550));

    EXPECT_FALSE(cadence.take(1550));
    EXPECT_EQ(cadence.dueInMs(1550), 100u);
    EXPECT_FALSE(cadence.take(1649));
    EXPECT_TRUE(cadence.take(1650));
}

// Exactly a period late is already a whole one: keeping the grid here would
// make the next deadline this same instant, and two measurements of it.
TEST(BaroCadenceGrid, ExactlyAPeriodLateStartsTheGridOver)
{
    BaroCadence cadence;
    cadence.start(1000, 100);
    ASSERT_TRUE(cadence.take(1000));

    ASSERT_TRUE(cadence.take(1200));

    EXPECT_FALSE(cadence.take(1200));
    EXPECT_TRUE(cadence.take(1300));
}

TEST(BaroCadenceGrid, AStopEndsIt)
{
    BaroCadence cadence;
    cadence.start(0, 100);
    ASSERT_TRUE(cadence.take(0));

    cadence.stop();

    EXPECT_FALSE(cadence.isRunning());
    EXPECT_FALSE(cadence.take(100));
    EXPECT_FALSE(cadence.take(100000));
}

// Slowing down: the measurement that was 100 ms away is now a whole new
// period after the last one.
TEST(BaroCadenceGrid, SlowingDownMovesTheNextOneOut)
{
    BaroCadence cadence;
    cadence.start(2000, 100);
    ASSERT_TRUE(cadence.take(2000));

    cadence.retime(1000);

    EXPECT_EQ(cadence.periodMs(), 1000u);
    EXPECT_FALSE(cadence.take(2100));
    EXPECT_FALSE(cadence.take(2999));
    EXPECT_TRUE(cadence.take(3000));
}

// Speeding up: the next one is a new period after the last, not a whole old
// period away.
TEST(BaroCadenceGrid, SpeedingUpBringsTheNextOneIn)
{
    BaroCadence cadence;
    cadence.start(2000, 1000);
    ASSERT_TRUE(cadence.take(2000));

    cadence.retime(100);

    EXPECT_FALSE(cadence.take(2099));
    EXPECT_TRUE(cadence.take(2100));
    EXPECT_TRUE(cadence.take(2200));
}

// Speeding up long after the last measurement: it is overdue on the new
// period, so it is taken now and the grid starts from here.
TEST(BaroCadenceGrid, SpeedingUpLateInAPeriodMeasuresAtOnce)
{
    BaroCadence cadence;
    cadence.start(2000, 1000);
    ASSERT_TRUE(cadence.take(2000));

    cadence.retime(100);

    ASSERT_TRUE(cadence.take(2700));
    EXPECT_FALSE(cadence.take(2799));
    EXPECT_TRUE(cadence.take(2800));
}

TEST(BaroCadenceGrid, RetimingAStoppedStreamDoesNotStartIt)
{
    BaroCadence cadence;

    cadence.retime(100);

    EXPECT_FALSE(cadence.isRunning());
    EXPECT_FALSE(cadence.take(1000));
}

// Deadlines are compared modulo 2^32, so the count passing its top between
// two of them changes nothing. The millisecond clock does not get there:
// what happens where it does turn over is the case after this one.
TEST(BaroCadenceGrid, KeepsItsGridPastTheTopOfTheCount)
{
    const uint32_t start = UINT32_MAX - 150u;

    BaroCadence cadence;
    cadence.start(start, 100);
    ASSERT_TRUE(cadence.take(start));

    EXPECT_FALSE(cadence.take(start + 99u));
    EXPECT_TRUE(cadence.take(start + 100u));

    // The next deadline is past the top of the count, and the clock is not
    // there yet: a small number ahead of a large one, not behind it.
    EXPECT_EQ(cadence.dueInMs(start + 100u), 100u);
    EXPECT_FALSE(cadence.take(start + 140u));
    EXPECT_FALSE(cadence.take(start + 199u));
    EXPECT_TRUE(cadence.take(start + 200u));
}

// The millisecond clock is the 1024 Hz tick counted in milliseconds: it
// returns to 0 at 4,194,304,000, about every 48.5 days. A deadline past that
// point reads as long overdue once the clock is back at 0, so one
// measurement comes early and the grid starts over from it.
TEST(BaroCadenceGrid, MeasuresOnceEarlyWhereTheClockTurnsOver)
{
    constexpr uint32_t kClockTurnsAtMs = 4194304000u;

    const uint32_t start = kClockTurnsAtMs - 150u;

    BaroCadence cadence;
    cadence.start(start, 100);
    ASSERT_TRUE(cadence.take(start));
    ASSERT_TRUE(cadence.take(start + 100u));

    // The next deadline is 50 ms past the turn, a count the clock never
    // shows.
    EXPECT_FALSE(cadence.take(kClockTurnsAtMs - 1u));

    EXPECT_TRUE(cadence.take(0u));

    EXPECT_FALSE(cadence.take(99u));
    EXPECT_TRUE(cadence.take(100u));
}
