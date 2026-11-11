/**
 ******************************************************************************
 * @file    BaroP0Adjuster_test.cpp
 * @brief   Host tests for the sea-level pressure (P0) calculator: when it
 *          gathers, what it averages, what a gap in the samples does, when
 *          a run is abandoned, and which altitudes it is started for.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "SensorLayer/Sensors/Barometer/BaroP0Adjuster.hpp"

using Sensor::BaroP0Adjuster;

namespace {

/// Feed n equal samples stepMs apart, moving t on. Returns what the last
/// update() said.
bool feed(BaroP0Adjuster& a, float pressureMbar, uint32_t n, uint32_t& t,
          uint32_t stepMs = 100)
{
    bool ready = false;

    for (uint32_t i = 0; i < n; ++i) {
        t += stepMs;
        ready = a.update(pressureMbar, t);
    }

    return ready;
}

} // namespace

TEST(BaroP0Adjuster, IsIdleUntilStarted)
{
    BaroP0Adjuster a;

    EXPECT_FALSE(a.isActive());
    EXPECT_FALSE(a.update(1013.25f, 100));
    EXPECT_FLOAT_EQ(a.getP0(), BaroP0Adjuster::skDefaultP0);
}

TEST(BaroP0Adjuster, IsActiveFromTheStart)
{
    BaroP0Adjuster a;

    a.start(0.0f, 0);

    EXPECT_TRUE(a.isActive());

    // The standard atmosphere until a sample says otherwise.
    EXPECT_FLOAT_EQ(a.getP0(), BaroP0Adjuster::skDefaultP0);
}

TEST(BaroP0Adjuster, CompletesOnTheLastSampleItNeeds)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 1013.25f, BaroP0Adjuster::skSamples - 1, t));
    EXPECT_TRUE(a.isActive());

    t += 100;
    EXPECT_TRUE(a.update(1013.25f, t));
    EXPECT_FALSE(a.isActive());

    // At sea level the reference is the pressure itself, in Pa.
    EXPECT_NEAR(a.getP0(), 101325.0f, 0.5f);
}

TEST(BaroP0Adjuster, TakesNothingMoreOnceComplete)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);
    ASSERT_TRUE(feed(a, 1000.0f, BaroP0Adjuster::skSamples, t));

    t += 100;
    EXPECT_FALSE(a.update(900.0f, t));
    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

TEST(BaroP0Adjuster, AveragesTheSamples)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 1000.0f, BaroP0Adjuster::skSamples / 2, t));
    EXPECT_TRUE(feed(a, 1010.0f, BaroP0Adjuster::skSamples / 2, t));

    EXPECT_NEAR(a.getP0(), 100500.0f, 0.5f);
}

TEST(BaroP0Adjuster, GivesAProvisionalValueWhileGathering)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 1000.0f, 5, t));
    EXPECT_TRUE(a.isActive());
    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

TEST(BaroP0Adjuster, AStartDropsARunInProgress)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);
    feed(a, 1000.0f, 4, t);

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 1020.0f, BaroP0Adjuster::skSamples - 1, t));

    t += 100;
    EXPECT_TRUE(a.update(1020.0f, t));
    EXPECT_NEAR(a.getP0(), 102000.0f, 0.5f);
}

// The stream stalled and came back: what was gathered before belongs to
// another moment, and the run starts over from the first sample after.
TEST(BaroP0Adjuster, AGapStartsTheRunOver)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 990.0f, 5, t));

    t += BaroP0Adjuster::skMaxGapMs + 1;
    EXPECT_FALSE(a.update(1000.0f, t));

    EXPECT_FALSE(feed(a, 1000.0f, BaroP0Adjuster::skSamples - 2, t));

    t += 100;
    EXPECT_TRUE(a.update(1000.0f, t));

    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

// The limit itself is still one run.
TEST(BaroP0Adjuster, AStepOfExactlyTheLimitIsNotAGap)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(0.0f, 0);

    EXPECT_FALSE(feed(a, 990.0f, 5, t));
    EXPECT_FALSE(feed(a, 1010.0f, 1, t, BaroP0Adjuster::skMaxGapMs));
    EXPECT_TRUE(feed(a, 1010.0f, 4, t));

    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

TEST(BaroP0Adjuster, AnAltitudeRaisesTheReferenceAboveThePressure)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(1000.0f, 0);
    EXPECT_TRUE(feed(a, 900.0f, BaroP0Adjuster::skSamples, t));

    // 900 mbar at 1000 m is about 1014.7 mbar at sea level.
    EXPECT_GT(a.getP0(), 90000.0f);
    EXPECT_NEAR(a.getP0(), 101468.0f, 100.0f);
}

// A reference worked out for an altitude nobody can be at would be wrong for
// every altitude read with it afterwards, and near the top of what the
// formula covers it is not even finite.
TEST(BaroP0Adjuster, AnAltitudeOutOfRangeIsNotStarted)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    for (float altitude : { BaroP0Adjuster::skMinAltitudeM - 0.5f,
                            BaroP0Adjuster::skMaxAltitudeM + 0.5f,
                            44329.9f, 44330.0f, 100000.0f, inf, -inf, nan }) {
        BaroP0Adjuster a;

        EXPECT_FALSE(a.start(altitude, 0)) << "altitude=" << altitude;
        EXPECT_FALSE(a.isActive()) << "altitude=" << altitude;
        EXPECT_FLOAT_EQ(a.getP0(), BaroP0Adjuster::skDefaultP0);
    }
}

TEST(BaroP0Adjuster, TheEndsOfTheRangeAreStarted)
{
    for (float altitude : { BaroP0Adjuster::skMinAltitudeM,
                            BaroP0Adjuster::skMaxAltitudeM }) {
        BaroP0Adjuster a;
        uint32_t t = 0;

        ASSERT_TRUE(a.start(altitude, 0)) << "altitude=" << altitude;
        EXPECT_TRUE(feed(a, 800.0f, BaroP0Adjuster::skSamples, t));
        EXPECT_TRUE(std::isfinite(a.getP0()));
    }
}

// A refusal is of the new run only: one already gathering goes on.
TEST(BaroP0Adjuster, ARefusedStartLeavesARunInProgress)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    ASSERT_TRUE(a.start(0.0f, 0));
    feed(a, 1000.0f, 4, t);

    EXPECT_FALSE(a.start(BaroP0Adjuster::skMaxAltitudeM + 1.0f, t));

    EXPECT_TRUE(a.isActive());
    EXPECT_TRUE(feed(a, 1000.0f, BaroP0Adjuster::skSamples - 4, t));
    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

TEST(BaroP0Adjuster, APressureOfZeroGivesNoResult)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    a.start(100.0f, 0);

    EXPECT_FALSE(feed(a, 0.0f, BaroP0Adjuster::skSamples, t));
    EXPECT_FLOAT_EQ(a.getP0(), BaroP0Adjuster::skDefaultP0);
}

// -----------------------------------------------------------------------------
// The deadline
// -----------------------------------------------------------------------------

// A run ends on its last sample. With the pressure gone nothing would end it,
// and whoever keeps the samples coming for it would go on asking.
TEST(BaroP0Adjuster, ARunWithNoSamplesIsAbandonedAfterTheDeadline)
{
    BaroP0Adjuster a;

    ASSERT_TRUE(a.start(0.0f, 1000));

    EXPECT_FALSE(a.expire(1000));
    EXPECT_FALSE(a.expire(1000 + BaroP0Adjuster::skDeadlineMs));
    EXPECT_TRUE(a.isActive());

    EXPECT_TRUE(a.expire(1000 + BaroP0Adjuster::skDeadlineMs + 1));
    EXPECT_FALSE(a.isActive());

    // Said once.
    EXPECT_FALSE(a.expire(1000 + BaroP0Adjuster::skDeadlineMs + 2));
}

TEST(BaroP0Adjuster, ExpiringAnIdleOneDoesNothing)
{
    BaroP0Adjuster a;

    EXPECT_FALSE(a.expire(1000000));
    EXPECT_FALSE(a.isActive());
}

// The pressure stopped partway and came back late: the sample that comes
// then ends the run without a result instead of going into it.
TEST(BaroP0Adjuster, ASampleAfterTheDeadlineEndsTheRunWithNoResult)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    ASSERT_TRUE(a.start(0.0f, 0));
    feed(a, 1000.0f, BaroP0Adjuster::skSamples - 1, t);
    ASSERT_TRUE(a.isActive());

    EXPECT_FALSE(a.update(1000.0f, BaroP0Adjuster::skDeadlineMs + 1));
    EXPECT_FALSE(a.isActive());
}

TEST(BaroP0Adjuster, ARunThatCompletesOnTheDeadlineIsKept)
{
    BaroP0Adjuster a;
    uint32_t t = 0;

    // Half a second apart, the last of them on the deadline itself.
    ASSERT_TRUE(a.start(0.0f, 0));
    feed(a, 1000.0f, BaroP0Adjuster::skSamples - 1, t, 500);
    ASSERT_LT(t, BaroP0Adjuster::skDeadlineMs);

    EXPECT_TRUE(a.update(1000.0f, BaroP0Adjuster::skDeadlineMs));
    EXPECT_NEAR(a.getP0(), 100000.0f, 0.5f);
}

// A measurement begun just before the start is handed over just after it,
// with its own, earlier instant. That is not a run long overdue.
TEST(BaroP0Adjuster, ASampleStampedBeforeTheStartDoesNotEndTheRun)
{
    BaroP0Adjuster a;

    ASSERT_TRUE(a.start(0.0f, 1000));

    EXPECT_FALSE(a.update(1000.0f, 960));
    EXPECT_TRUE(a.isActive());
    EXPECT_FALSE(a.expire(960));
}

TEST(BaroP0Adjuster, AStartGivesTheRunANewDeadline)
{
    BaroP0Adjuster a;

    ASSERT_TRUE(a.start(0.0f, 0));
    ASSERT_TRUE(a.start(0.0f, 4000));

    EXPECT_FALSE(a.expire(BaroP0Adjuster::skDeadlineMs + 1));
    EXPECT_FALSE(a.expire(4000 + BaroP0Adjuster::skDeadlineMs));
    EXPECT_TRUE(a.expire(4000 + BaroP0Adjuster::skDeadlineMs + 1));
}
