/**
 ******************************************************************************
 * @file    SourceTick_test.cpp
 * @brief   Host tests for Sensor::SourceTick, the sampling clock of a
 *          sensor that takes its sample on the sweep.
 *
 *          What it must give: one sample per interval on a fixed grid
 *          however late or early the sweep that notices it, no burst
 *          after a long stall, and a deadline the sweep can sleep to.
 *
 ******************************************************************************
 */

#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "SensorLayer/Core/SourceTick.hpp"

namespace {

constexpr uint32_t kLead = 3;

// Start at t0 and take the first sweep there, as the driver would.
void begin(Sensor::SourceTick& tick, uint32_t intervalMs, uint32_t t0)
{
    tick.start(intervalMs);
    EXPECT_FALSE(tick.check(t0, kLead));
}

} // namespace

TEST(SourceTick, StoppedIsNeverDueAndAsksForNoWake)
{
    Sensor::SourceTick tick;

    EXPECT_FALSE(tick.isActive());
    EXPECT_FALSE(tick.check(1000, kLead));
    EXPECT_EQ(tick.dueInMs(1000), Sensor::SourceTick::kNone);
}

TEST(SourceTick, AStartAsksForASweepToTakeItUp)
{
    Sensor::SourceTick tick;
    tick.start(200);

    EXPECT_TRUE(tick.isActive());
    EXPECT_EQ(tick.intervalMs(), 200u);
    EXPECT_EQ(tick.dueInMs(5000), 0u);

    // Taken up: the first sample is an interval later.
    EXPECT_FALSE(tick.check(5000, kLead));
    EXPECT_EQ(tick.dueInMs(5000), 200u);
    EXPECT_EQ(tick.dueInMs(5150), 50u);
}

TEST(SourceTick, DueWithinTheLeadAndNotBefore)
{
    Sensor::SourceTick tick;
    begin(tick, 200, 1000);

    EXPECT_FALSE(tick.check(1200 - kLead - 1, kLead));
    EXPECT_TRUE(tick.check(1200 - kLead, kLead));

    // Once per deadline.
    EXPECT_FALSE(tick.check(1200, kLead));
    EXPECT_EQ(tick.dueInMs(1200), 200u);
}

// The point of it: sweeps that come late by varying amounts do not stretch the
// step. The deadlines stay on the grid, so the count of samples over a long
// run is exactly the time over the interval.
TEST(SourceTick, LateSweepsDoNotStretchTheStep)
{
    std::mt19937 rng(3);
    std::uniform_int_distribution<int> lateness(0, 150);

    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    uint32_t samples = 0;

    // A sweep at each deadline, late by up to three quarters of an interval,
    // and some sweeps between them for other reasons.
    for (uint32_t k = 1; k <= 1000; ++k) {
        const uint32_t deadline = k * 200;

        if (tick.check(deadline - 100, kLead)) {
            ++samples;
        }
        if (tick.check(deadline + static_cast<uint32_t>(lateness(rng)), kLead)) {
            ++samples;
        }
    }

    EXPECT_EQ(samples, 1000u);
}

// The same grid against a clock that is sampled only when the sweep wakes for
// what dueInMs() asked, with the wake up to 2 ms early or 1 ms late.
TEST(SourceTick, SleepingToTheDeadlineKeepsTheGrid)
{
    std::mt19937 rng(5);
    std::uniform_int_distribution<int> wakeError(-2, 1);

    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    uint32_t now = 0;
    std::vector<uint32_t> sampledAt;

    while (now < 200 * 500) {
        if (tick.check(now, kLead)) {
            sampledAt.push_back(now);
        }

        const uint32_t sleep = tick.dueInMs(now);
        const int woken = static_cast<int>((sleep != 0) ? sleep : 1) + wakeError(rng);
        now += static_cast<uint32_t>((woken > 0) ? woken : 1);
    }

    ASSERT_GE(sampledAt.size(), 499u);

    for (size_t i = 1; i < sampledAt.size(); ++i) {
        const uint32_t step = sampledAt[i] - sampledAt[i - 1];
        EXPECT_GE(step, 200u - 2 * kLead - 2) << "sample " << i;
        EXPECT_LE(step, 200u + 2 * kLead + 2) << "sample " << i;
    }
}

// A sweep a whole interval or more late gives one sample and starts the grid
// over from there, rather than catching up with a burst.
TEST(SourceTick, ALongStallGivesOneSampleNotABurst)
{
    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    EXPECT_TRUE(tick.check(200, kLead));

    // Nothing sweeps for over two seconds.
    EXPECT_TRUE(tick.check(2450, kLead));
    EXPECT_FALSE(tick.check(2450, kLead));
    EXPECT_FALSE(tick.check(2600, kLead));
    EXPECT_TRUE(tick.check(2650, kLead));
}

// Late by less than an interval: the next deadline stays where the grid put
// it, so the step after a late sample is short and the average holds.
TEST(SourceTick, ALateSampleIsFollowedByAnOnTimeOne)
{
    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    EXPECT_TRUE(tick.check(350, kLead));
    EXPECT_EQ(tick.dueInMs(350), 50u);
    EXPECT_TRUE(tick.check(400, kLead));
}

TEST(SourceTick, ARestartTakesTheNewIntervalFromThePresent)
{
    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    EXPECT_TRUE(tick.check(200, kLead));

    tick.start(1000);
    EXPECT_EQ(tick.dueInMs(250), 0u);
    EXPECT_FALSE(tick.check(250, kLead));
    EXPECT_FALSE(tick.check(400, kLead));
    EXPECT_TRUE(tick.check(1250, kLead));
}

TEST(SourceTick, StopEndsItAndStartBeginsAgain)
{
    Sensor::SourceTick tick;
    begin(tick, 200, 0);

    tick.stop();
    EXPECT_FALSE(tick.isActive());
    EXPECT_FALSE(tick.check(200, kLead));
    EXPECT_EQ(tick.dueInMs(200), Sensor::SourceTick::kNone);

    tick.start(200);
    EXPECT_FALSE(tick.check(300, kLead));
    EXPECT_TRUE(tick.check(500, kLead));
}

// The millisecond clock wraps at 4,194,304,000, not at 2^32. A deadline set
// just before the wrap is a value the clock never reaches, so after the wrap
// it reads as long overdue: one sample comes early, and the grid starts over
// from it rather than bursting.
TEST(SourceTick, TheClockWrapGivesOneEarlySampleAndTheGridStartsOver)
{
    constexpr uint32_t kClockWrapMs = 4194304000u;

    Sensor::SourceTick tick;
    begin(tick, 200, kClockWrapMs - 500);

    EXPECT_TRUE(tick.check(kClockWrapMs - 300, kLead));
    EXPECT_TRUE(tick.check(kClockWrapMs - 100, kLead));

    // The next deadline would fall at 100 after the wrap. At 20 it fires,
    // 80 ms early, once.
    EXPECT_TRUE(tick.check(20, kLead));
    EXPECT_FALSE(tick.check(21, kLead));

    // From there the grid runs from 20.
    EXPECT_EQ(tick.dueInMs(20), 200u);
    EXPECT_FALSE(tick.check(200, kLead));
    EXPECT_TRUE(tick.check(220, kLead));
    EXPECT_TRUE(tick.check(420, kLead));
}

TEST(SourceTick, TheStepIsTheIntervalInMicroseconds)
{
    Sensor::SourceTick tick;
    EXPECT_EQ(tick.stepUs(), 0u);

    tick.start(200);
    EXPECT_EQ(tick.stepUs(), 200000u);

    // Past 32 bits of microseconds: reported as none, not wrapped.
    tick.start(5000u * 1000u);
    EXPECT_EQ(tick.stepUs(), 0u);
}

TEST(SourceTick, ZeroIntervalIsTakenAsOneMillisecond)
{
    Sensor::SourceTick tick;
    tick.start(0);

    EXPECT_TRUE(tick.isActive());
    EXPECT_EQ(tick.intervalMs(), 1u);
}
