/**
 ******************************************************************************
 * @file    MagRecoveryPolicy_test.cpp
 * @brief   Host tests for the magnetometer's recovery policy: the deadline a
 *          stream is judged by, the backoff between attempts, and the ladder
 *          from a cheap fix to giving up. Decided from the arguments alone,
 *          so the whole escalation runs with no part, RTOS or real time.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "Magnetometer/MagRecoveryPolicy.hpp"

using Action = MagRecoveryPolicy::Action;

namespace {

// Four periods: far below the floor at the fastest rung, above at the slowest.
constexpr uint32_t kFastPeriodUs = 2500;     // 400 Hz
constexpr uint32_t kSlowPeriodUs = 640000;   // 1.5625 Hz
constexpr uint32_t kDefPeriodUs  = 40000;    // 25 Hz, the component's default

/// A stream that is up and has just delivered; tests move one thing away.
MagRecoveryPolicy::State healthy(uint32_t periodUs = kDefPeriodUs)
{
    MagRecoveryPolicy::State s;
    s.wanted            = true;
    s.running           = true;
    s.sinceLastSampleMs = 0;
    s.attempts          = 0;
    s.appliedPeriodUs   = periodUs;

    return s;
}

} // namespace

// -----------------------------------------------------------------------------
// The deadline
// -----------------------------------------------------------------------------

TEST(MagRecoveryDeadline, ScalesWithThePeriodOnceThatBeatsTheFloor)
{
    EXPECT_EQ(MagRecoveryPolicy::deadlineMs(kSlowPeriodUs), 2560u);
    EXPECT_EQ(MagRecoveryPolicy::deadlineMs(320000u), 1280u);
}

TEST(MagRecoveryDeadline, NeverDropsBelowTheFloor)
{
    // Every rung from 25 Hz up lands under the floor on four periods.
    EXPECT_EQ(MagRecoveryPolicy::deadlineMs(kFastPeriodUs),
              MagRecoveryPolicy::skFloorMs);
    EXPECT_EQ(MagRecoveryPolicy::deadlineMs(kDefPeriodUs),
              MagRecoveryPolicy::skFloorMs);
    EXPECT_EQ(MagRecoveryPolicy::deadlineMs(0u), MagRecoveryPolicy::skFloorMs);
}

// A deadline inside bus contention would trip on a healthy stream.
TEST(MagRecoveryDeadline, IsWellClearOfASingleBurstOnTheSharedBus)
{
    for (uint32_t periodUs : { 2500u, 5000u, 10000u, 20000u, 40000u,
                               80000u, 160000u, 320000u, 640000u }) {
        EXPECT_GE(MagRecoveryPolicy::deadlineMs(periodUs), 100u)
                << "periodUs=" << periodUs;
    }
}

// -----------------------------------------------------------------------------
// The backoff
// -----------------------------------------------------------------------------

TEST(MagRecoveryBackoff, FirstLookWaitsExactlyTheDeadline)
{
    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 0),
              MagRecoveryPolicy::deadlineMs(kDefPeriodUs));
}

TEST(MagRecoveryBackoff, DoublesPerAttemptUpToTheCeiling)
{
    const uint32_t base = MagRecoveryPolicy::deadlineMs(kDefPeriodUs);

    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 1), base * 2);
    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 2), base * 4);
    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 3), base * 8);

    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 4),
              base * MagRecoveryPolicy::skMaxBackoff);
    EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, 9),
              base * MagRecoveryPolicy::skMaxBackoff);
}

// Giving up caps the count, but a shift as wide as the type is undefined, so
// the answer must not depend on that cap.
TEST(MagRecoveryBackoff, AbsurdAttemptCountsStillAnswerTheCeiling)
{
    const uint32_t ceiling = MagRecoveryPolicy::deadlineMs(kDefPeriodUs) *
                             MagRecoveryPolicy::skMaxBackoff;

    for (uint32_t attempts : { 31u, 32u, 64u, 1000u, UINT32_MAX }) {
        EXPECT_EQ(MagRecoveryPolicy::waitMs(kDefPeriodUs, attempts), ceiling)
                << "attempts=" << attempts;
    }
}

TEST(MagRecoveryBackoff, NeverShrinks)
{
    uint32_t previous = 0;
    for (uint32_t attempts = 0; attempts <= 12; attempts++) {
        const uint32_t wait = MagRecoveryPolicy::waitMs(kSlowPeriodUs, attempts);
        EXPECT_GE(wait, previous) << "attempts=" << attempts;
        previous = wait;
    }
}

// -----------------------------------------------------------------------------
// The decision
// -----------------------------------------------------------------------------

TEST(MagRecoveryDecide, LeavesAStreamAloneWhileSamplesKeepArriving)
{
    EXPECT_EQ(MagRecoveryPolicy::decide(healthy()), Action::NOTHING);
}

// The boundary is pinned because the loop wakes on exactly this number.
TEST(MagRecoveryDecide, WaitsOutTheWholeDeadlineBeforeActing)
{
    MagRecoveryPolicy::State s = healthy();

    s.sinceLastSampleMs = MagRecoveryPolicy::deadlineMs(kDefPeriodUs) - 1;
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::NOTHING);

    s.sinceLastSampleMs = MagRecoveryPolicy::deadlineMs(kDefPeriodUs);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RELEASE_LATCH);
}

// Nothing is owed for a stream nobody asked for, however long the silence.
TEST(MagRecoveryDecide, DoesNothingWhenNoStreamIsWanted)
{
    MagRecoveryPolicy::State s = healthy();
    s.wanted            = false;
    s.sinceLastSampleMs = 100000;
    s.attempts          = 5;

    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::NOTHING);
}

// With the chip down there is no latch to release, so the first action is the
// rebuild - the case of a start that never came up.
TEST(MagRecoveryDecide, SkipsTheCheapFixWhenNoStreamIsConfigured)
{
    MagRecoveryPolicy::State s = healthy();
    s.running           = false;
    s.sinceLastSampleMs = MagRecoveryPolicy::deadlineMs(kDefPeriodUs);

    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RESTART);
}

TEST(MagRecoveryDecide, TriesTheCheapFixOnlyOnce)
{
    MagRecoveryPolicy::State s = healthy();

    s.attempts          = 0;
    s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(kDefPeriodUs, 0);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RELEASE_LATCH);

    s.attempts          = 1;
    s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(kDefPeriodUs, 1);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RESTART);
}

TEST(MagRecoveryDecide, GivesUpOnlyOnceTheAttemptsAreSpent)
{
    MagRecoveryPolicy::State s = healthy();

    s.attempts          = MagRecoveryPolicy::skMaxAttempts - 1;
    s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(kDefPeriodUs, s.attempts);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RESTART);

    s.attempts          = MagRecoveryPolicy::skMaxAttempts;
    s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(kDefPeriodUs, s.attempts);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::GIVE_UP);
}

// Giving up is a verdict about attempts, not about time.
TEST(MagRecoveryDecide, DoesNotGiveUpOnSilenceAlone)
{
    MagRecoveryPolicy::State s = healthy();
    s.running           = false;
    s.attempts          = 0;
    s.sinceLastSampleMs = 3600000;   // an hour

    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RESTART);
}

// The whole ladder with the worker's waits: one cheap fix, then restarts, then
// a verdict, never stalling part way or exceeding the budget.
TEST(MagRecoveryDecide, WalksTheLadderOnceAndStops)
{
    MagRecoveryPolicy::State s = healthy();

    uint32_t latchReleases = 0;
    uint32_t restarts      = 0;

    for (uint32_t step = 0; step < 100; step++) {
        s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(s.appliedPeriodUs,
                                                        s.attempts);

        const Action action = MagRecoveryPolicy::decide(s);
        if (action == Action::GIVE_UP) {
            break;
        }

        ASSERT_NE(action, Action::NOTHING)
                << "the ladder stalled at attempt " << s.attempts;

        if (action == Action::RELEASE_LATCH) {
            latchReleases++;
        } else {
            restarts++;
            // A restart that did not take leaves the stream down.
            s.running = false;
        }

        s.attempts++;
    }

    EXPECT_EQ(latchReleases, 1u);
    EXPECT_EQ(restarts, MagRecoveryPolicy::skMaxAttempts - 1);
    EXPECT_EQ(s.attempts, MagRecoveryPolicy::skMaxAttempts);
}

// The tests above follow the constants; this pins the total in seconds, loose
// enough to retune within and tight enough to catch an order of magnitude. The
// sum covers the waits before each attempt; the wait before GIVE_UP adds one.
TEST(MagRecoveryPolicy_Budget, TotalTimeBeforeGivingUpStaysInTheMinutes)
{
    for (uint32_t periodUs : { kFastPeriodUs, kDefPeriodUs, kSlowPeriodUs }) {
        uint32_t totalMs = 0;
        for (uint32_t attempts = 0;
             attempts < MagRecoveryPolicy::skMaxAttempts; attempts++) {
            totalMs += MagRecoveryPolicy::waitMs(periodUs, attempts);
        }

        EXPECT_GE(totalMs,  20000u) << "periodUs=" << periodUs;
        EXPECT_LE(totalMs, 600000u) << "periodUs=" << periodUs;
    }
}

// A sample mid-stall resets the count: the ladder restarts at the cheap fix.
TEST(MagRecoveryDecide, StartsOverAfterASampleArrives)
{
    // One short of the budget, so this is not about where the budget ends.
    const uint32_t spent = MagRecoveryPolicy::skMaxAttempts - 1;

    MagRecoveryPolicy::State s = healthy();
    s.attempts          = spent;
    s.sinceLastSampleMs = MagRecoveryPolicy::waitMs(kDefPeriodUs, spent);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RESTART);

    // What the component does on every sample read.
    s.attempts          = 0;
    s.sinceLastSampleMs = 0;
    s.running           = true;
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::NOTHING);

    s.sinceLastSampleMs = MagRecoveryPolicy::deadlineMs(kDefPeriodUs);
    EXPECT_EQ(MagRecoveryPolicy::decide(s), Action::RELEASE_LATCH);
}
