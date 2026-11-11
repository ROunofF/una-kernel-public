/**
 ******************************************************************************
 * @file    MagRateHandover_test.cpp
 * @brief   Host tests for which rate a magnetometer sample was converted at
 *          around a change of rate. The streams are the stamps the part gave
 *          on the watch across such changes, in microseconds.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "Magnetometer/MagRateHandover.hpp"

// A steady stream is of the rate the part is set to.
TEST(MagRateHandover, ASteadyStreamIsOfTheAppliedRate)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(13942357, 10000), 10000u);
    EXPECT_EQ(handover.periodFor(13952344, 10000), 10000u);
    EXPECT_EQ(handover.periodFor(13962315, 10000), 10000u);
}

// 100 Hz to 3.125 Hz: the conversion in flight comes on the old grid, the
// next one 19.4 ms after it, and from there the part converts every 320 ms.
TEST(MagRateHandover, SlowingDownTheSampleInFlightIsOfTheOldRate)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(13962315, 10000), 10000u);
    EXPECT_EQ(handover.periodFor(13972287, 10000), 10000u);

    handover.changed(10000);

    EXPECT_EQ(handover.periodFor(13982259, 320000), 10000u);
    EXPECT_EQ(handover.periodFor(14001693, 320000), 320000u);
    EXPECT_EQ(handover.periodFor(14320915, 320000), 320000u);
    EXPECT_EQ(handover.periodFor(14640133, 320000), 320000u);
}

// 3.125 Hz to 100 Hz: the part finishes the slow conversion first, most of
// an old period after the rate was written, and converts fast from there.
TEST(MagRateHandover, SpeedingUpTheSampleInFlightIsOfTheOldRate)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(26341134, 320000), 320000u);
    EXPECT_EQ(handover.periodFor(26660346, 320000), 320000u);

    handover.changed(320000);

    EXPECT_EQ(handover.periodFor(26979558, 10000), 320000u);
    EXPECT_EQ(handover.periodFor(26990117, 10000), 10000u);
    EXPECT_EQ(handover.periodFor(27000076, 10000), 10000u);
}

// 25 Hz to 100 Hz and back, as measured.
TEST(MagRateHandover, BetweenNeighbouringRates)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(21303371, 40000), 40000u);
    EXPECT_EQ(handover.periodFor(21343271, 40000), 40000u);

    handover.changed(40000);

    EXPECT_EQ(handover.periodFor(21383175, 10000), 40000u);
    EXPECT_EQ(handover.periodFor(21393893, 10000), 10000u);
    EXPECT_EQ(handover.periodFor(21403871, 10000), 10000u);

    handover.changed(10000);

    EXPECT_EQ(handover.periodFor(21413849, 40000), 10000u);
    EXPECT_EQ(handover.periodFor(21433140, 40000), 40000u);
    EXPECT_EQ(handover.periodFor(21473046, 40000), 40000u);
}

// A sample read before the change but handed on after it, and the one in
// flight behind it: both carry on the old grid.
TEST(MagRateHandover, EverySampleOnTheOldGridIsOfTheOldRate)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(1000000, 10000), 10000u);

    handover.changed(10000);

    EXPECT_EQ(handover.periodFor(1009970, 320000), 10000u);
    EXPECT_EQ(handover.periodFor(1019940, 320000), 10000u);
    EXPECT_EQ(handover.periodFor(1039370, 320000), 320000u);

    // The old rate is done with: a step that happens to match it later is
    // of the applied rate.
    EXPECT_EQ(handover.periodFor(1049370, 320000), 320000u);
}

// Two changes before the first has shown: the conversion in flight is still
// of the rate before the first.
TEST(MagRateHandover, ASecondChangeDoesNotMoveTheOldRate)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(1000000, 10000), 10000u);

    handover.changed(10000);
    handover.changed(320000);

    EXPECT_EQ(handover.periodFor(1009970, 40000), 10000u);
    EXPECT_EQ(handover.periodFor(1029400, 40000), 40000u);
}

// A change before the stream gave a sample: there is no old grid to carry on.
TEST(MagRateHandover, AChangeBeforeTheFirstSampleLeavesNothingOld)
{
    MagRateHandover handover;
    handover.start();
    handover.changed(10000);

    // Stamped an old period after the count of time starts at, which is not
    // a sample before it.
    EXPECT_EQ(handover.periodFor(9970, 320000), 320000u);
    EXPECT_EQ(handover.periodFor(329970, 320000), 320000u);
}

// A new stream forgets a change the last one never showed.
TEST(MagRateHandover, AStartForgetsAPendingChange)
{
    MagRateHandover handover;
    handover.start();

    EXPECT_EQ(handover.periodFor(1000000, 10000), 10000u);

    handover.changed(10000);
    handover.start();

    // Where the old grid would have had its next sample.
    EXPECT_EQ(handover.periodFor(1009970, 320000), 320000u);
    EXPECT_EQ(handover.periodFor(1329970, 320000), 320000u);
}
