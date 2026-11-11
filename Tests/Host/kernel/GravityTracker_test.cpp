/**
 ******************************************************************************
 * @file    GravityTracker_test.cpp
 * @brief   Host tests for GravityTracker: the filter, the magnitude gate, the
 *          age limit and the hold limit, and then the reason it exists - a
 *          bearing that stays put while the watch is tilted, where the level
 *          bearing turns round. Plain calls with made-up timestamps, so every
 *          run is the same.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "Compass/GravityTracker.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticField.hpp"

namespace {

using State = GravityTracker::State;

constexpr float    kG        = GravityTracker::kStandardGravity;
constexpr uint64_t kStart    = 10000000U;   // clear of zero, so nothing wraps
constexpr uint64_t k25HzUs   = 40000U;
constexpr uint64_t k100HzUs  = 10000U;
constexpr float    kDegToRad = 0.017453292519943295f;

struct Vec {
    float x;
    float y;
    float z;
};

/// Push the same reading at a fixed period from `from` for `count` samples.
/// Returns the instant of the last one.
uint64_t feed(GravityTracker& t, uint64_t from, uint64_t periodUs,
              unsigned count, Vec a)
{
    uint64_t now = from;

    for (unsigned i = 0; i < count; ++i) {
        now = from + (static_cast<uint64_t>(i) * periodUs);
        t.push(now, a.x, a.y, a.z);
    }

    return now;
}

State query(const GravityTracker& t, uint64_t tUs, Vec& g)
{
    return t.query(tUs, g.x, g.y, g.z);
}

// ---------------------------------------------------------------------------
// The tracker on its own
// ---------------------------------------------------------------------------

TEST(GravityTracker, NothingPushedIsNone)
{
    GravityTracker t;
    Vec g{1.0f, 2.0f, 3.0f};

    EXPECT_EQ(query(t, kStart, g), State::NONE);
    EXPECT_FLOAT_EQ(g.x, 1.0f);   // untouched
}

TEST(GravityTracker, FirstReadingInTheGateIsGivenAsItIs)
{
    GravityTracker t;
    t.push(kStart, 1.0f, 2.0f, 9.5f);

    Vec g{};
    ASSERT_EQ(query(t, kStart, g), State::STEADY);
    EXPECT_FLOAT_EQ(g.x, 1.0f);
    EXPECT_FLOAT_EQ(g.y, 2.0f);
    EXPECT_FLOAT_EQ(g.z, 9.5f);
}

TEST(GravityTracker, ResetForgetsEverything)
{
    GravityTracker t;
    t.push(kStart, 0.0f, 0.0f, kG);
    t.reset();

    Vec g{};
    EXPECT_EQ(query(t, kStart, g), State::NONE);
}

/// Fraction of a step the filter has covered `elapsedUs` after it, at a rate.
float stepFraction(uint64_t periodUs, uint64_t elapsedUs)
{
    GravityTracker t;
    const float s = std::sin(45.0f * kDegToRad) * kG;
    const float c = std::cos(45.0f * kDegToRad) * kG;

    uint64_t now = feed(t, kStart, periodUs, 50U, {0.0f, 0.0f, kG});
    const unsigned steps = static_cast<unsigned>(elapsedUs / periodUs);
    now = feed(t, now + periodUs, periodUs, steps, {0.0f, s, c});

    Vec g{};
    EXPECT_EQ(query(t, now, g), State::STEADY);

    return g.y / s;
}

/// The time constant a run of samples turns out to have been filtered with.
float stepTimeConstant(uint64_t periodUs, uint64_t elapsedUs)
{
    const double covered = stepFraction(periodUs, elapsedUs);
    const double spanS   = static_cast<double>(elapsedUs) * 1e-6;

    return static_cast<float>(-spanS / std::log(1.0 - covered));
}

TEST(GravityTracker, FilterFollowsAStepWithItsTimeConstant)
{
    // A filter stepped once a sample is slower than the one it stands for, by
    // more the coarser the steps; never by more than half a step. Both rates
    // are held to that, which is what carrying the step across rates means.
    // 8 steps at 25 Hz and 32 at 100 Hz both span 0.32 s.
    const float at25  = stepTimeConstant(k25HzUs, 320000U);
    const float at100 = stepTimeConstant(k100HzUs, 320000U);

    EXPECT_GE(at25, GravityTracker::kTauS);
    EXPECT_LE(at25, GravityTracker::kTauS + (0.5e-6f * k25HzUs));

    EXPECT_GE(at100, GravityTracker::kTauS);
    EXPECT_LE(at100, GravityTracker::kTauS + (0.5e-6f * k100HzUs));
}

TEST(GravityTracker, AGapRestartsTheFilter)
{
    GravityTracker t;
    uint64_t now = feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});

    now += GravityTracker::kMaxAgeUs + 1U;
    t.push(now, 0.0f, kG, 0.0f);

    Vec g{};
    ASSERT_EQ(query(t, now, g), State::STEADY);
    EXPECT_FLOAT_EQ(g.y, kG);
    EXPECT_FLOAT_EQ(g.z, 0.0f);
}

TEST(GravityTracker, AtTheAgeLimitIsGivenAndPastItIsNone)
{
    GravityTracker t;
    t.push(kStart, 0.0f, 0.0f, kG);

    Vec g{};
    EXPECT_EQ(query(t, kStart + GravityTracker::kMaxAgeUs, g), State::STEADY);
    EXPECT_EQ(query(t, kStart + GravityTracker::kMaxAgeUs + 1U, g),
              State::NONE);

    // Either side of the sample: a field measured a little before the latest
    // reading is paired with it too.
    EXPECT_EQ(query(t, kStart - GravityTracker::kMaxAgeUs, g), State::STEADY);
    EXPECT_EQ(query(t, kStart - GravityTracker::kMaxAgeUs - 1U, g),
              State::NONE);
}

TEST(GravityTracker, TheGateIsInclusiveAtBothEnds)
{
    // Just inside each end, so float rounding of the bound does not decide.
    const float lo = GravityTracker::kMinG * kG * 1.0001f;
    const float hi = GravityTracker::kMaxG * kG * 0.9999f;
    Vec g{};

    GravityTracker a;
    a.push(kStart, 0.0f, 0.0f, lo);
    EXPECT_EQ(query(a, kStart, g), State::STEADY);

    GravityTracker b;
    b.push(kStart, 0.0f, 0.0f, hi);
    EXPECT_EQ(query(b, kStart, g), State::STEADY);

    GravityTracker c;
    c.push(kStart, 0.0f, 0.0f, GravityTracker::kMinG * kG * 0.999f);
    EXPECT_EQ(query(c, kStart, g), State::MOVING);

    GravityTracker d;
    d.push(kStart, 0.0f, 0.0f, GravityTracker::kMaxG * kG * 1.001f);
    EXPECT_EQ(query(d, kStart, g), State::MOVING);
}

TEST(GravityTracker, ADisturbanceSettlesAfterItsLastSpike)
{
    GravityTracker t;
    uint64_t now = feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});

    now += k25HzUs;
    const uint64_t spike = now;
    t.push(spike, 0.0f, 0.0f, 2.0f * kG);

    Vec g{};
    EXPECT_EQ(query(t, spike, g), State::MOVING);

    // In the gate again, but not yet settled.
    const unsigned beforeSettle =
            static_cast<unsigned>(GravityTracker::kSettleUs / k25HzUs) - 1U;
    now = feed(t, spike + k25HzUs, k25HzUs, beforeSettle, {0.0f, 0.0f, kG});
    ASSERT_LT(now - spike, GravityTracker::kSettleUs);
    EXPECT_EQ(query(t, now, g), State::MOVING);

    now = spike + GravityTracker::kSettleUs;
    t.push(now, 0.0f, 0.0f, kG);
    EXPECT_EQ(query(t, now, g), State::STEADY);
}

TEST(GravityTracker, ReadingsOutsideTheGateNeverReachTheFilter)
{
    GravityTracker t;
    uint64_t now = feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});

    // A swing: large sideways readings, then the watch at rest face-sideways.
    now = feed(t, now + k25HzUs, k25HzUs, 5U, {15.0f, 0.0f, kG});
    now = feed(t, now + k25HzUs, k25HzUs, 10U, {kG, 0.0f, 0.0f});

    Vec g{};
    ASSERT_EQ(query(t, now, g), State::STEADY);

    // Restarted from the first reading back in the gate: nothing of the swing
    // and nothing of the attitude before it.
    EXPECT_FLOAT_EQ(g.x, kG);
    EXPECT_FLOAT_EQ(g.y, 0.0f);
    EXPECT_FLOAT_EQ(g.z, 0.0f);
}

TEST(GravityTracker, ADisturbanceHeldTooLongIsNone)
{
    GravityTracker t;
    uint64_t now = feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});

    // A spike every 200 ms, never far enough apart to settle.
    const uint64_t first = now + k25HzUs;
    Vec g{};

    for (uint64_t at = first; at < first + 3000000U; at += k25HzUs) {
        const bool spike = ((at - first) % 200000U) == 0U;
        t.push(at, 0.0f, 0.0f, spike ? 2.0f * kG : kG);

        const State expected = ((at - first) < GravityTracker::kMaxHoldUs)
                ? State::MOVING : State::NONE;
        ASSERT_EQ(query(t, at, g), expected) << "at +" << (at - first) << " us";
    }
}

TEST(GravityTracker, WaitingForLateReadingsIsNotMovement)
{
    GravityTracker t;
    uint64_t now = feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});

    // A disturbance whose readings span just short of the hold limit, with a
    // spike every 200 ms so it never settles, ending on a spike.
    const uint64_t first = now + k25HzUs;
    const uint64_t last  = first + GravityTracker::kMaxHoldUs - k25HzUs;

    for (uint64_t at = first; at <= last; at += k25HzUs) {
        const bool spike = (((at - first) % 200000U) == 0U) || (at == last);
        t.push(at, 0.0f, 0.0f, spike ? 2.0f * kG : kG);
    }

    // Asked about a moment past the hold limit, measured from the first
    // reading, while the next reading has not arrived yet. The readings seen
    // so far span less than the limit, so this is still movement.
    const uint64_t asked = last + (2U * k25HzUs);
    ASSERT_GE(asked - first, GravityTracker::kMaxHoldUs);
    ASSERT_LE(asked - last, GravityTracker::kMaxAgeUs);

    Vec g{};
    EXPECT_EQ(query(t, asked, g), State::MOVING);
}

TEST(GravityTracker, NotANumberIsDropped)
{
    GravityTracker t;
    t.push(kStart, 0.0f, 0.0f, kG);
    t.push(kStart + k25HzUs, std::nanf(""), 0.0f, kG);
    t.push(kStart + (2U * k25HzUs), 0.0f, INFINITY, kG);

    Vec g{};
    ASSERT_EQ(query(t, kStart + (2U * k25HzUs), g), State::STEADY);
    EXPECT_FLOAT_EQ(g.z, kG);
}

TEST(GravityTracker, TimeRunningBackwardsRestarts)
{
    GravityTracker t;
    feed(t, kStart, k25HzUs, 25U, {0.0f, 0.0f, kG});
    t.push(kStart - k25HzUs, 0.0f, kG, 0.0f);

    Vec g{};
    ASSERT_EQ(query(t, kStart - k25HzUs, g), State::STEADY);
    EXPECT_FLOAT_EQ(g.y, kG);
}

// ---------------------------------------------------------------------------
// The bearing it is for
// ---------------------------------------------------------------------------

/// The field in the UK: 49 uT at 66.5 degrees of inclination, no declination.
constexpr float kFieldUt       = 49.0f;
constexpr float kInclinationDeg = 66.5f;

float dot(Vec a, Vec b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

/**
 * A watch pointing 12 o'clock at `headingDeg` (clockwise from north) and
 * tilted by `tiltDeg` about its own 3-9 o'clock axis, positive raising
 * 12 o'clock - the face turning towards someone looking along it. The world
 * is east, north, up. Returns what the magnetometer and the accelerometer
 * read in the watch's axes.
 */
void watchReads(float headingDeg, float tiltDeg, Vec& field, Vec& accel)
{
    const float h = headingDeg * kDegToRad;
    const float t = tiltDeg * kDegToRad;

    const Vec up{0.0f, 0.0f, 1.0f};
    const Vec yLevel{std::sin(h), std::cos(h), 0.0f};

    const Vec x{std::cos(h), -std::sin(h), 0.0f};
    const Vec y{(yLevel.x * std::cos(t)) + (up.x * std::sin(t)),
                (yLevel.y * std::cos(t)) + (up.y * std::sin(t)),
                (yLevel.z * std::cos(t)) + (up.z * std::sin(t))};
    const Vec z{(-yLevel.x * std::sin(t)) + (up.x * std::cos(t)),
                (-yLevel.y * std::sin(t)) + (up.y * std::cos(t)),
                (-yLevel.z * std::sin(t)) + (up.z * std::cos(t))};

    const float inc = kInclinationDeg * kDegToRad;
    const Vec world{0.0f, kFieldUt * std::cos(inc), -kFieldUt * std::sin(inc)};
    const Vec gravity{0.0f, 0.0f, kG};   // an accelerometer at rest reads up

    field = {dot(world, x), dot(world, y), dot(world, z)};
    accel = {dot(gravity, x), dot(gravity, y), dot(gravity, z)};
}

float angleApart(float a, float b)
{
    float d = std::fmod(std::fabs(a - b), 360.0f);
    return (d > 180.0f) ? (360.0f - d) : d;
}

/// Hold the watch at each tilt for two seconds at 25 Hz, then take a bearing
/// through the tracker and demand the heading back.
void expectHeadingHeld(float headingDeg, float fromDeg, float toDeg,
                       float stepDeg)
{
    GravityTracker t;
    uint64_t now = kStart;

    for (float tilt = fromDeg; (stepDeg > 0.0f) ? (tilt <= toDeg)
                                                : (tilt >= toDeg);
         tilt += stepDeg) {
        Vec field{};
        Vec accel{};
        watchReads(headingDeg, tilt, field, accel);

        now = feed(t, now + k25HzUs, k25HzUs, 50U, accel);

        Vec g{};
        ASSERT_EQ(query(t, now, g), State::STEADY) << "tilt " << tilt;

        using SDK::SensorDataParser::MagneticField;

        float xh = 0.0f;
        float yh = 0.0f;
        ASSERT_TRUE(MagneticField::levelProject(field.x, field.y, field.z,
                                                g.x, g.y, g.z, xh, yh))
                << "tilt " << tilt;
        ASSERT_TRUE(MagneticField::hasDirection(xh, yh)) << "tilt " << tilt;

        EXPECT_LT(angleApart(MagneticField::bearingDeg(xh, yh), headingDeg), 1.0f)
                << "heading " << headingDeg << ", tilt " << tilt;
    }
}

TEST(GravityTrackerBearing, FacingNorthTiltedTowardsTheWearer)
{
    expectHeadingHeld(0.0f, 0.0f, 75.0f, 5.0f);
}

TEST(GravityTrackerBearing, FacingSouthTiltedAway)
{
    expectHeadingHeld(180.0f, 0.0f, -75.0f, -5.0f);
}

TEST(GravityTrackerBearing, FacingEastAndWestTiltedBothWays)
{
    expectHeadingHeld(90.0f, -45.0f, 45.0f, 5.0f);
    expectHeadingHeld(270.0f, -45.0f, 45.0f, 5.0f);
}

TEST(GravityTrackerBearing, TheLevelBearingTurnsRoundWhereThisHolds)
{
    // The failure the tracker is here for, kept so this file shows it: facing
    // north, 30 degrees of tilt towards the wearer reads south.
    using SDK::SensorDataParser::MagneticField;

    Vec field{};
    Vec accel{};
    watchReads(0.0f, 30.0f, field, accel);

    EXPECT_GT(angleApart(MagneticField::bearingDeg(field.x, field.y), 0.0f),
              90.0f);
}

} // namespace
