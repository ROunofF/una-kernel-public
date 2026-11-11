/**
 * Host unit tests for Ble::RevolutionRateEstimator — the shared delta-revs /
 * delta-time derivation behind CPS and CSC cadence and wheel rate.
 * Pure logic, no BlueNRG/OS deps.
 *
 * The wheel trace below is REAL DATA captured from a Garmin Speed Sensor 2,
 * used in preference to invented values so the arithmetic is checked against
 * a device rather than against our reading of the spec.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/RevolutionRateEstimator.hpp"

namespace {

using Ble::RevolutionRateEstimator;
using Ble::RevTickRate;

using CounterWidth = RevolutionRateEstimator::CounterWidth;

// Captured CSC Measurement payloads, decoded. Host times are the log's own
// notification timestamps, relative to the first sample.
struct WheelSample {
    uint32_t hostMs;
    uint32_t revs;
    uint16_t ticks;
};

// From `01-10-02-00-00-C3-2E` and successors: flags 0x01, uint32 LE revs,
// uint16 LE event time at 1/1024 s.
constexpr WheelSample kGarminWheelTrace[] = {
    {     0, 528, 11971 },
    {  2160, 530, 21155 },
    {  3600, 531, 21751 },
    {  4318, 532, 22974 },
    {  5277, 534, 24012 },
    { 15121, 535, 33495 },
};

RevolutionRateEstimator makeWheelEstimator()
{
    return RevolutionRateEstimator(CounterWidth::Bits32, RevTickRate::Hz1024);
}

RevolutionRateEstimator makeCrankEstimator()
{
    return RevolutionRateEstimator(CounterWidth::Bits16, RevTickRate::Hz1024);
}

} // namespace

TEST(RevolutionRateEstimator, FirstSampleYieldsNoRate)
{
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 528, 11971);
    EXPECT_FALSE(e.getSample().valid);
}

TEST(RevolutionRateEstimator, DerivesCapturedGarminWheelTrace)
{
    auto e = makeWheelEstimator();

    // 60 * ticksPerSecond * dRevs / dTicks for each successive pair.
    const float expected[] = {
        122880.0f / 9184.0f,   // +2 rev / 9184 ticks  ->  13.38 rpm
        61440.0f / 596.0f,     // +1 rev /  596 ticks  -> 103.09 rpm
        61440.0f / 1223.0f,    // +1 rev / 1223 ticks  ->  50.24 rpm
        122880.0f / 1038.0f,   // +2 rev / 1038 ticks  -> 118.38 rpm
        61440.0f / 9483.0f,    // +1 rev / 9483 ticks  ->   6.48 rpm
    };

    e.onMeasurement(kGarminWheelTrace[0].hostMs, kGarminWheelTrace[0].revs,
            kGarminWheelTrace[0].ticks);

    for (std::size_t i = 1; i < std::size(kGarminWheelTrace); ++i) {
        const WheelSample &s = kGarminWheelTrace[i];
        e.onMeasurement(s.hostMs, s.revs, s.ticks);
        auto out = e.getSample();
        ASSERT_TRUE(out.valid) << "interval " << i;
        EXPECT_NEAR(out.rpm, expected[i - 1], 0.01f) << "interval " << i;
    }
}

TEST(RevolutionRateEstimator, FirstIntervalMayPredateSubscription)
{
    // The captured 528->530 interval spans 8.97 s of EVENT time but only 2.16 s
    // of host time, because revolution 528's timestamp predates the moment we
    // subscribed. That is legitimate and must still produce a rate: event time
    // is the basis, host time only guards against an ambiguous multi-wrap gap.
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 528, 11971);
    e.onMeasurement(2160, 530, 21155);

    auto out = e.getSample();
    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.rpm, 122880.0f / 9184.0f, 0.01f);
}

TEST(RevolutionRateEstimator, EventTimeRolloverIsHandled)
{
    // Event time wraps every 64 s at 1/1024 s, so this is the routine path.
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 1000, 65000);
    e.onMeasurement(1100, 1001, 500);   // wrapped: dTicks = 1036

    auto out = e.getSample();
    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.rpm, 61440.0f / 1036.0f, 0.01f);
}

TEST(RevolutionRateEstimator, CrankCounterWrapsAtSixteenBits)
{
    // Crank revolutions are uint16 on the wire and wrap every 65536.
    auto e = makeCrankEstimator();
    e.onMeasurement(0, 65530, 1000);
    e.onMeasurement(1000, 4, 2024);   // wrapped: dRevs = 10, dTicks = 1024

    auto out = e.getSample();
    ASSERT_TRUE(out.valid);
    EXPECT_NEAR(out.rpm, 600.0f, 0.01f);   // 10 rev in 1.0 s
}

TEST(RevolutionRateEstimator, Bits32CounterDoesNotWrapAtSixteenBits)
{
    // The same counter values under a 32-bit width are a decrease, i.e. a
    // sensor reset rather than a wrap. The implied delta is absurd, so no rate
    // is published -- proving the width parameter genuinely changes the modulus.
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 65530, 1000);
    e.onMeasurement(1000, 4, 2024);

    EXPECT_FALSE(e.getSample().valid);
}

TEST(RevolutionRateEstimator, TickRateScalesTheDerivedRate)
{
    // The CPS-wheel (1/2048 s) versus CSC-wheel (1/1024 s) trap: identical
    // bytes must yield rates differing by exactly two.
    RevolutionRateEstimator slow(CounterWidth::Bits32, RevTickRate::Hz1024);
    RevolutionRateEstimator fast(CounterWidth::Bits32, RevTickRate::Hz2048);

    slow.onMeasurement(0, 100, 0);
    slow.onMeasurement(1000, 101, 1024);
    fast.onMeasurement(0, 100, 0);
    fast.onMeasurement(1000, 101, 1024);

    EXPECT_NEAR(slow.getSample().rpm, 60.0f, 0.01f);    // 1024 ticks = 1.0 s
    EXPECT_NEAR(fast.getSample().rpm, 120.0f, 0.01f);   // 1024 ticks = 0.5 s
}

TEST(RevolutionRateEstimator, StallEmitsValidZeroAfterTimeout)
{
    // Measured behaviour: a stationary sensor repeats byte-identical payloads
    // at its normal rate, so both deltas are zero. Using the real repeated
    // fixture (rev 534 @ 24012 ticks).
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 532, 22974);
    e.onMeasurement(959, 534, 24012);
    ASSERT_TRUE(e.getSample().valid);
    EXPECT_NEAR(e.getSample().rpm, 122880.0f / 1038.0f, 0.01f);

    // Repeats within the stall timeout hold the last rate.
    for (uint32_t t = 1679; t < 4959; t += 720) {
        e.onMeasurement(t, 534, 24012);
    }
    EXPECT_NEAR(e.getSample().rpm, 122880.0f / 1038.0f, 0.01f);
    EXPECT_TRUE(e.getSample().valid);

    // Past it, a confirmed stall: zero, and still VALID -- the rider stopped
    // pedalling, which is a fact rather than a dropout.
    e.onMeasurement(5000, 534, 24012);
    EXPECT_TRUE(e.getSample().valid);
    EXPECT_FLOAT_EQ(e.getSample().rpm, 0.0f);
}

TEST(RevolutionRateEstimator, SilenceGoesInvalidRatherThanZero)
{
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 100, 0);
    e.onMeasurement(1000, 101, 1024);
    ASSERT_TRUE(e.getSample().valid);

    e.tickMs(1500);                     // still fresh
    EXPECT_TRUE(e.getSample().valid);

    e.tickMs(1000 + 5000);              // dataTimeoutMs since the last sample
    EXPECT_FALSE(e.getSample().valid);  // no data, NOT a valid zero
    EXPECT_FLOAT_EQ(e.getSample().rpm, 0.0f);
}

TEST(RevolutionRateEstimator, TickReportsStallWhileNotificationsContinue)
{
    RevolutionRateEstimator e(CounterWidth::Bits32, RevTickRate::Hz1024);
    e.onMeasurement(0, 100, 0);
    e.onMeasurement(1000, 101, 1024);

    // Notifications keep arriving with frozen counters; the tick must resolve
    // the stall even though samples are never absent.
    for (uint32_t t = 1720; t <= 5320; t += 720) {
        e.onMeasurement(t, 101, 1024);
        e.tickMs(t);
    }
    EXPECT_TRUE(e.getSample().valid);
    EXPECT_FLOAT_EQ(e.getSample().rpm, 0.0f);
}

TEST(RevolutionRateEstimator, AmbiguousGapResyncsInsteadOfGuessing)
{
    // A gap beyond one wrap of the event-time counter (64 s at 1/1024 s) makes
    // dTicks ambiguous. Resync rather than publish an inflated rate; the next
    // interval recovers.
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 100, 0);
    e.onMeasurement(1000, 101, 1024);
    ASSERT_NEAR(e.getSample().rpm, 60.0f, 0.01f);

    e.onMeasurement(1000 + 70000, 102, 2048);   // 70 s later: ambiguous
    EXPECT_NEAR(e.getSample().rpm, 60.0f, 0.01f);   // unchanged, not inflated

    e.onMeasurement(1000 + 70000 + 1000, 103, 3072);   // clean interval again
    EXPECT_NEAR(e.getSample().rpm, 60.0f, 0.01f);
    EXPECT_TRUE(e.getSample().valid);
}

TEST(RevolutionRateEstimator, ZeroTickDeltaWithAdvancingRevsIsRejected)
{
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 100, 5000);
    e.onMeasurement(500, 101, 5000);   // revs advanced, event time did not
    EXPECT_FALSE(e.getSample().valid);
}

TEST(RevolutionRateEstimator, ImplausibleRateIsRejected)
{
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 100, 0);
    e.onMeasurement(1000, 101, 1024);
    ASSERT_NEAR(e.getSample().rpm, 60.0f, 0.01f);

    // 1 revolution in a single tick implies ~61440 rpm: keep the last estimate.
    e.onMeasurement(1100, 102, 1025);
    EXPECT_NEAR(e.getSample().rpm, 60.0f, 0.01f);
}

TEST(RevolutionRateEstimator, ResetClearsState)
{
    auto e = makeWheelEstimator();
    e.onMeasurement(0, 100, 0);
    e.onMeasurement(1000, 101, 1024);
    ASSERT_TRUE(e.getSample().valid);

    e.reset();
    EXPECT_FALSE(e.getSample().valid);
    EXPECT_FLOAT_EQ(e.getSample().rpm, 0.0f);

    // And the next measurement is treated as a first sample again.
    e.onMeasurement(2000, 200, 2048);
    EXPECT_FALSE(e.getSample().valid);
}
