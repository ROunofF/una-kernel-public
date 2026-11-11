/**
 * Host unit tests for Sensor::LatencyAggregate: the latency a driver owes all
 * its listeners.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "SensorLayer/Core/LatencyAggregate.hpp"

// ---------------------------------------------------------------------------
// Sensor::LatencyAggregate
// ---------------------------------------------------------------------------

TEST(LatencyAggregate, NothingAddedIsNoRequest)
{
    Sensor::LatencyAggregate agg;

    EXPECT_EQ(agg.value(), 0u);
}

TEST(LatencyAggregate, TightestListenerWins)
{
    Sensor::LatencyAggregate agg;
    agg.add(1000);
    agg.add(100);
    agg.add(500);

    EXPECT_EQ(agg.value(), 100u);
}

TEST(LatencyAggregate, OrderDoesNotMatter)
{
    Sensor::LatencyAggregate agg;
    agg.add(100);
    agg.add(1000);

    EXPECT_EQ(agg.value(), 100u);
}

// A listener faster than a millisecond defaults to a latency of 0. That is the
// most demanding request there is, not the absence of one.
TEST(LatencyAggregate, ZeroIsTheTightestRequestNotAnAbsentOne)
{
    Sensor::LatencyAggregate alone;
    alone.add(0);
    EXPECT_EQ(alone.value(), 1u);

    Sensor::LatencyAggregate mixed;
    mixed.add(500);
    mixed.add(0);
    EXPECT_EQ(mixed.value(), 1u);
}
