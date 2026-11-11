/**
 ******************************************************************************
 * @file    SourceStepTrack_test.cpp
 * @brief   Host tests for Sensor::SourceStepTrack: which sampling step a
 *          sample held in a sensor's queue was taken at. All times are
 *          microseconds.
 ******************************************************************************
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "SensorLayer/Core/SourceStepTrack.hpp"

namespace {

constexpr uint64_t kMs = 1000;

} // namespace

TEST(SourceStepTrack, NothingNotedIsNoStep)
{
    Sensor::SourceStepTrack track;

    EXPECT_EQ(track.stepAt(0), 0u);
    EXPECT_EQ(track.stepAt(5000 * kMs), 0u);
}

TEST(SourceStepTrack, OneStepHoldsForEverySample)
{
    Sensor::SourceStepTrack track;
    track.note(1000 * kMs, 10000);

    EXPECT_EQ(track.stepAt(1000 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(9000 * kMs), 10000u);

    // And for one from before it was noted: there is no other to give.
    EXPECT_EQ(track.stepAt(500 * kMs), 10000u);
}

// What the class is for: a sample taken before a change of rate and published
// after it is still counted against the step it was taken at.
TEST(SourceStepTrack, ASampleFromBeforeAChangeKeepsItsStep)
{
    Sensor::SourceStepTrack track;
    track.note(1000 * kMs, 10000);
    track.note(2000 * kMs, 320000);

    EXPECT_EQ(track.stepAt(1990 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(2000 * kMs), 320000u);
    EXPECT_EQ(track.stepAt(2320 * kMs), 320000u);
}

TEST(SourceStepTrack, TheSameStepAgainIsNotAChange)
{
    Sensor::SourceStepTrack track;
    track.note(1000 * kMs, 10000);
    track.note(2000 * kMs, 320000);

    // Every batch notes its step; the start of the stretch stays where it was.
    track.note(2320 * kMs, 320000);
    track.note(2640 * kMs, 320000);

    EXPECT_EQ(track.stepAt(1990 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(2000 * kMs), 320000u);
}

TEST(SourceStepTrack, SeveralChangesAreToldApart)
{
    Sensor::SourceStepTrack track;
    track.note(1000 * kMs, 40000);
    track.note(2000 * kMs, 10000);
    track.note(3000 * kMs, 20000);
    track.note(4000 * kMs, 40000);

    EXPECT_EQ(track.stepAt(1500 * kMs), 40000u);
    EXPECT_EQ(track.stepAt(2500 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(3500 * kMs), 20000u);
    EXPECT_EQ(track.stepAt(4500 * kMs), 40000u);
}

// More changes than are kept: the oldest goes, and a sample from before what
// is left gets the oldest step that is.
TEST(SourceStepTrack, TheOldestChangeMakesRoom)
{
    Sensor::SourceStepTrack track;

    for (uint32_t i = 0; i <= Sensor::SourceStepTrack::skDepth; ++i) {
        track.note((1000 + 1000 * i) * kMs, 10000 * (i + 1));
    }

    EXPECT_EQ(track.stepAt(1500 * kMs), 20000u);
    EXPECT_EQ(track.stepAt(2500 * kMs), 20000u);
    EXPECT_EQ(track.stepAt(3500 * kMs), 30000u);
    EXPECT_EQ(track.stepAt(5500 * kMs), 50000u);
}

// A stream whose stamps start over leaves nothing of the changes before it.
TEST(SourceStepTrack, StampsThatStartOverDropWhatWasKept)
{
    Sensor::SourceStepTrack track;
    track.note(5000 * kMs, 10000);
    track.note(6000 * kMs, 20000);

    track.note(1000 * kMs, 40000);

    EXPECT_EQ(track.stepAt(500 * kMs), 40000u);
    EXPECT_EQ(track.stepAt(1000 * kMs), 40000u);
    EXPECT_EQ(track.stepAt(5500 * kMs), 40000u);
}

// The same when the step is the one it was: a sample of the new stream is
// behind every change kept, and must not be given the oldest of them.
TEST(SourceStepTrack, StampsThatStartOverAtTheSameStepDropWhatWasKept)
{
    Sensor::SourceStepTrack track;
    track.note(5000 * kMs, 10000);
    track.note(6000 * kMs, 320000);

    track.note(1000 * kMs, 320000);

    EXPECT_EQ(track.stepAt(1000 * kMs), 320000u);
    EXPECT_EQ(track.stepAt(1320 * kMs), 320000u);
}

// A source that stops reporting a step is a change like any other.
TEST(SourceStepTrack, NoStepIsNotedToo)
{
    Sensor::SourceStepTrack track;
    track.note(1000 * kMs, 10000);
    track.note(2000 * kMs, 0);

    EXPECT_EQ(track.stepAt(1500 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(2500 * kMs), 0u);
}

// Stamps past 2^32 us, about 71.6 minutes of uptime, are compared whole.
TEST(SourceStepTrack, StampsPastThirtyTwoBitsAreComparedWhole)
{
    constexpr uint64_t kWrap = static_cast<uint64_t>(UINT32_MAX) + 1;

    Sensor::SourceStepTrack track;
    track.note(kWrap - 1000 * kMs, 10000);
    track.note(kWrap + 1000 * kMs, 20000);

    EXPECT_EQ(track.stepAt(kWrap - 500 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(kWrap + 500 * kMs), 10000u);
    EXPECT_EQ(track.stepAt(kWrap + 1500 * kMs), 20000u);
}
