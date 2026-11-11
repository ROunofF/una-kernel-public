/**
 ******************************************************************************
 * @file    BaroRecoveryPolicy_test.cpp
 * @brief   Host tests for the barometer's recovery policy: what counts as a
 *          fault, the wait before each power cycle, what clears the attempts
 *          spent, and the ladder from the first power cycle to giving up.
 *          Decided from the arguments alone, so the whole escalation runs with
 *          no part, RTOS or real time.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "Barometer/BaroRecoveryPolicy.hpp"

using Action = BaroRecoveryPolicy::Action;
using State  = BaroRecoveryPolicy::State;

namespace {

/// A stream that is up and measuring; tests move one thing away.
State healthy()
{
    State s;
    s.wanted = true;
    s.up     = true;

    return s;
}

/// As many fresh measurements as show the part is measuring again.
void measureAgain(State& s)
{
    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMinFreshReads; i++) {
        BaroRecoveryPolicy::onReadOk(s, false);
    }
}

/// What the owner of the stream does about a fault, until the policy has
/// nothing more to ask for. `comesUp` answers each power cycle.
/// Returns the power cycles performed.
template <typename ComesUp>
uint32_t recover(State& s, ComesUp comesUp)
{
    uint32_t cycles = 0;

    while (BaroRecoveryPolicy::decide(s) == Action::POWER_CYCLE) {
        BaroRecoveryPolicy::onPowerCycled(s, comesUp());
        cycles++;

        if (cycles > 100) {
            ADD_FAILURE() << "the ladder never ended";
            break;
        }
    }

    return cycles;
}

} // namespace

// -----------------------------------------------------------------------------
// The wait
// -----------------------------------------------------------------------------

// One fault may be a glitch, so the first power cycle is not held back.
TEST(BaroRecoveryWait, FirstAttemptIsNotWaitedFor)
{
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(0), 0u);
}

TEST(BaroRecoveryWait, DoublesPerAttemptUpToTheCeiling)
{
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(1),  2000u);
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(2),  4000u);
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(3),  8000u);
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(4), 16000u);

    EXPECT_EQ(BaroRecoveryPolicy::waitMs(5), BaroRecoveryPolicy::skMaxWaitMs);
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(9), BaroRecoveryPolicy::skMaxWaitMs);
}

// Giving up caps the count, but a shift as wide as the type is undefined, so
// the answer must not depend on that cap.
TEST(BaroRecoveryWait, AbsurdAttemptCountsStillAnswerTheCeiling)
{
    for (uint32_t attempts : { 31u, 32u, 33u, 64u, 1000u, UINT32_MAX }) {
        EXPECT_EQ(BaroRecoveryPolicy::waitMs(attempts),
                  BaroRecoveryPolicy::skMaxWaitMs)
                << "attempts=" << attempts;
    }
}

TEST(BaroRecoveryWait, NeverShrinks)
{
    uint32_t previous = 0;
    for (uint32_t attempts = 0; attempts <= 40; attempts++) {
        const uint32_t wait = BaroRecoveryPolicy::waitMs(attempts);
        EXPECT_GE(wait, previous) << "attempts=" << attempts;
        previous = wait;
    }
}

// The tests above follow the constants; this pins the total in seconds, loose
// enough to retune within and tight enough to catch an order of magnitude.
TEST(BaroRecoveryWait, TotalTimeBeforeGivingUpStaysInTheMinutes)
{
    uint32_t totalMs = 0;
    for (uint32_t attempts = 0;
         attempts < BaroRecoveryPolicy::skMaxAttempts; attempts++) {
        totalMs += BaroRecoveryPolicy::waitMs(attempts);
    }

    EXPECT_GE(totalMs,  60000u);
    EXPECT_LE(totalMs, 600000u);
}

// -----------------------------------------------------------------------------
// The decision
// -----------------------------------------------------------------------------

TEST(BaroRecoveryDecide, LeavesAMeasuringStreamAlone)
{
    EXPECT_EQ(BaroRecoveryPolicy::decide(healthy()), Action::NOTHING);
}

// Nothing is owed for a stream nobody asked for, whatever state it is in.
TEST(BaroRecoveryDecide, DoesNothingWhenNoStreamIsWanted)
{
    State s = healthy();
    s.wanted        = false;
    s.up            = false;
    s.failedReads   = 100;
    s.repeatedReads = 100;
    s.attempts      = 5;

    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);
}

TEST(BaroRecoveryDecide, ToleratesFailuresShortOfTheLimit)
{
    State s = healthy();

    s.failedReads = BaroRecoveryPolicy::skMaxFailedReads - 1;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);

    s.failedReads = BaroRecoveryPolicy::skMaxFailedReads;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
}

// A still watch in a still room can read the same value twice; a long run of
// it is a part that answers and does not convert.
TEST(BaroRecoveryDecide, ToleratesRepeatsShortOfTheLimit)
{
    State s = healthy();

    s.repeatedReads = BaroRecoveryPolicy::skMaxRepeatedReads - 1;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);

    s.repeatedReads = BaroRecoveryPolicy::skMaxRepeatedReads;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
}

// A part that did not come up has nothing to read, so it is not left to fail
// reads first - the case of a start that never came up.
TEST(BaroRecoveryDecide, PowerCyclesAPartThatDidNotComeUp)
{
    State s = healthy();
    s.up = false;

    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
}

TEST(BaroRecoveryDecide, GivesUpOnlyOnceTheAttemptsAreSpent)
{
    State s = healthy();
    s.up = false;

    s.attempts = BaroRecoveryPolicy::skMaxAttempts - 1;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);

    s.attempts = BaroRecoveryPolicy::skMaxAttempts;
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::GIVE_UP);
}

// Giving up is a verdict about a part that is faulty now, not about attempts
// spent on a fault it has since got over.
TEST(BaroRecoveryDecide, DoesNotGiveUpOnAPartThatIsMeasuring)
{
    State s = healthy();
    s.attempts = BaroRecoveryPolicy::skMaxAttempts;

    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);
}

// -----------------------------------------------------------------------------
// The bookkeeping
// -----------------------------------------------------------------------------

TEST(BaroRecoveryRecord, AGoodReadEndsARunOfFailures)
{
    State s = healthy();

    BaroRecoveryPolicy::onReadFailed(s);
    BaroRecoveryPolicy::onReadFailed(s);
    EXPECT_EQ(s.failedReads, 2u);

    BaroRecoveryPolicy::onReadOk(s, false);
    EXPECT_EQ(s.failedReads, 0u);
}

// The limit is on failures in a row: two, a good read, and two more is not
// three.
TEST(BaroRecoveryRecord, ScatteredFailuresDoNotAddUp)
{
    State s = healthy();

    for (int round = 0; round < 10; round++) {
        BaroRecoveryPolicy::onReadFailed(s);
        BaroRecoveryPolicy::onReadFailed(s);
        BaroRecoveryPolicy::onReadOk(s, false);

        EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING)
                << "round=" << round;
    }
}

TEST(BaroRecoveryRecord, AChangedReadEndsARunOfRepeats)
{
    State s = healthy();

    BaroRecoveryPolicy::onReadOk(s, true);
    BaroRecoveryPolicy::onReadOk(s, true);
    EXPECT_EQ(s.repeatedReads, 2u);

    BaroRecoveryPolicy::onReadOk(s, false);
    EXPECT_EQ(s.repeatedReads, 0u);
}

// A part that drops a read between two frozen ones is still frozen.
TEST(BaroRecoveryRecord, AFailureDoesNotEndARunOfRepeats)
{
    State s = healthy();

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxRepeatedReads; i++) {
        BaroRecoveryPolicy::onReadOk(s, true);
        BaroRecoveryPolicy::onReadFailed(s);
    }

    // The failures never stood three in a row; the repeats did add up.
    EXPECT_LT(s.failedReads, BaroRecoveryPolicy::skMaxFailedReads);
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
}

TEST(BaroRecoveryRecord, OnlyARunOfChangedReadsClearsTheAttempts)
{
    State s = healthy();
    s.attempts = 4;

    BaroRecoveryPolicy::onReadFailed(s);
    EXPECT_EQ(s.attempts, 4u);

    BaroRecoveryPolicy::onReadOk(s, true);
    EXPECT_EQ(s.attempts, 4u);

    for (uint32_t i = 1; i < BaroRecoveryPolicy::skMinFreshReads; i++) {
        BaroRecoveryPolicy::onReadOk(s, false);
        EXPECT_EQ(s.attempts, 4u) << "after " << i;
    }

    BaroRecoveryPolicy::onReadOk(s, false);
    EXPECT_EQ(s.attempts, 0u);
}

// The run is of measurements in a row: a failure or a repeat in it starts it
// over.
TEST(BaroRecoveryRecord, AFailureOrARepeatStartsTheRunOfChangedReadsOver)
{
    for (bool failure : { true, false }) {
        State s = healthy();
        s.attempts = 4;

        for (uint32_t i = 1; i < BaroRecoveryPolicy::skMinFreshReads; i++) {
            BaroRecoveryPolicy::onReadOk(s, false);
        }

        if (failure) {
            BaroRecoveryPolicy::onReadFailed(s);
        } else {
            BaroRecoveryPolicy::onReadOk(s, true);
        }

        for (uint32_t i = 1; i < BaroRecoveryPolicy::skMinFreshReads; i++) {
            BaroRecoveryPolicy::onReadOk(s, false);
        }

        EXPECT_EQ(s.attempts, 4u) << "failure=" << failure;

        BaroRecoveryPolicy::onReadOk(s, false);
        EXPECT_EQ(s.attempts, 0u) << "failure=" << failure;
    }
}

// What was measured before a power cycle says nothing about the part after
// it.
TEST(BaroRecoveryRecord, APowerCycleStartsTheRunOfChangedReadsOver)
{
    State s = healthy();

    for (uint32_t i = 1; i < BaroRecoveryPolicy::skMinFreshReads; i++) {
        BaroRecoveryPolicy::onReadOk(s, false);
    }

    BaroRecoveryPolicy::onPowerCycled(s, true);
    BaroRecoveryPolicy::onReadOk(s, false);

    EXPECT_EQ(s.attempts, 1u);
}

TEST(BaroRecoveryRecord, APowerCycleIsAnAttemptAndStartsBothRunsOver)
{
    State s = healthy();
    s.failedReads   = BaroRecoveryPolicy::skMaxFailedReads;
    s.repeatedReads = 2;
    s.attempts      = 1;

    BaroRecoveryPolicy::onPowerCycled(s, true);

    EXPECT_EQ(s.attempts, 2u);
    EXPECT_TRUE(s.up);
    EXPECT_EQ(s.failedReads, 0u);
    EXPECT_EQ(s.repeatedReads, 0u);
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);
}

TEST(BaroRecoveryRecord, APowerCycleThatFailsLeavesThePartDown)
{
    State s = healthy();

    BaroRecoveryPolicy::onPowerCycled(s, false);

    EXPECT_FALSE(s.up);
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
}

// -----------------------------------------------------------------------------
// The ladder
// -----------------------------------------------------------------------------

// A part that never comes up again: every attempt is spent on it and no more.
TEST(BaroRecoveryLadder, ADeadPartIsGivenUpOnceTheAttemptsAreSpent)
{
    State s = healthy();
    s.up = false;

    const uint32_t cycles = recover(s, [] { return false; });

    EXPECT_EQ(cycles, BaroRecoveryPolicy::skMaxAttempts);
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::GIVE_UP);
}

// A part that comes up after every power cycle and then answers with one
// frozen value. Its reads succeed, so only the rule that a repeat is not a
// sign of life lets the attempts run out.
TEST(BaroRecoveryLadder, APartFrozenOnOneValueIsGivenUp)
{
    State s = healthy();

    uint32_t cycles = 0;
    uint32_t reads  = 0;

    while (BaroRecoveryPolicy::decide(s) != Action::GIVE_UP) {
        ASSERT_LT(reads, 1000u) << "a frozen part was never given up";

        if (BaroRecoveryPolicy::decide(s) == Action::POWER_CYCLE) {
            BaroRecoveryPolicy::onPowerCycled(s, true);
            cycles++;
            continue;
        }

        BaroRecoveryPolicy::onReadOk(s, true);
        reads++;
    }

    EXPECT_EQ(cycles, BaroRecoveryPolicy::skMaxAttempts);
    EXPECT_EQ(reads, (BaroRecoveryPolicy::skMaxAttempts + 1)
                     * BaroRecoveryPolicy::skMaxRepeatedReads);
}

// A part that measures once after every power cycle and then stops answering.
// One measurement does not show it is back, so each fault is met further up
// the ladder and the attempts run out.
TEST(BaroRecoveryLadder, APartThatMeasuresOnceAfterEachPowerCycleIsGivenUp)
{
    State s = healthy();

    uint32_t cycles = 0;

    while (BaroRecoveryPolicy::decide(s) != Action::GIVE_UP) {
        ASSERT_LT(cycles, 100u) << "a flapping part was never given up";

        if (BaroRecoveryPolicy::decide(s) == Action::POWER_CYCLE) {
            EXPECT_EQ(s.attempts, cycles);

            BaroRecoveryPolicy::onPowerCycled(s, true);
            cycles++;

            BaroRecoveryPolicy::onReadOk(s, false);
            continue;
        }

        BaroRecoveryPolicy::onReadFailed(s);
    }

    EXPECT_EQ(cycles, BaroRecoveryPolicy::skMaxAttempts);
}

// The same with a part that freezes on a new value after each power cycle:
// one fresh measurement, then repeats of it.
TEST(BaroRecoveryLadder, APartThatFreezesOnANewValueEachTimeIsGivenUp)
{
    State s = healthy();

    uint32_t cycles = 0;
    uint32_t reads  = 0;

    while (BaroRecoveryPolicy::decide(s) != Action::GIVE_UP) {
        ASSERT_LT(reads, 1000u) << "a part freezing anew was never given up";

        if (BaroRecoveryPolicy::decide(s) == Action::POWER_CYCLE) {
            BaroRecoveryPolicy::onPowerCycled(s, true);
            cycles++;

            BaroRecoveryPolicy::onReadOk(s, false);
            continue;
        }

        BaroRecoveryPolicy::onReadOk(s, true);
        reads++;
    }

    EXPECT_EQ(cycles, BaroRecoveryPolicy::skMaxAttempts);
}

// A part that a power cycle did bring back: the next fault is met from the
// start of the ladder, with no wait.
TEST(BaroRecoveryLadder, APartThatMeasuresAgainStartsOver)
{
    State s = healthy();
    s.up = false;

    // Down for three attempts, then up.
    uint32_t left = 3;
    const uint32_t cycles = recover(s, [&left] { return --left == 0; });

    EXPECT_EQ(cycles, 3u);
    EXPECT_EQ(s.attempts, 3u);
    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::NOTHING);

    measureAgain(s);
    EXPECT_EQ(s.attempts, 0u);

    // The next fault.
    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        BaroRecoveryPolicy::onReadFailed(s);
    }

    EXPECT_EQ(BaroRecoveryPolicy::decide(s), Action::POWER_CYCLE);
    EXPECT_EQ(BaroRecoveryPolicy::waitMs(s.attempts), 0u);
}
