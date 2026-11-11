/**
 ******************************************************************************
 * @file    FaceTilt_test.cpp
 * @brief   Host tests for FaceTilt: the angle between the face and the sky for
 *          the arm raised, the wrist turned and both, in any unit, and what
 *          gravity that is not a direction reads as.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>

#include "Compass/FaceTilt.hpp"

namespace {

constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kG        = 9.80665f;

/// What the accelerometer reads with the arm raised by `raiseDeg` (a turn
/// about 3-9 o'clock) and then the wrist turned by `rollDeg` about the forearm
/// (12-6 o'clock): up, in watch axes.
struct Vec {
    float x;
    float y;
    float z;
};

Vec gravityAt(float raiseDeg, float rollDeg)
{
    const float a = raiseDeg * kDegToRad;
    const float b = rollDeg * kDegToRad;

    // Up in the watch's axes: raising 12 o'clock puts gravity along +Y;
    // turning the wrist lowering 3 o'clock puts it along +X.
    return {std::cos(a) * std::sin(b) * kG,
            std::sin(a) * kG,
            std::cos(a) * std::cos(b) * kG};
}

float tiltAt(float raiseDeg, float rollDeg)
{
    const Vec g = gravityAt(raiseDeg, rollDeg);
    return FaceTilt::degrees(g.x, g.y, g.z);
}

TEST(FaceTilt, IsTheAngleFromFacingUp)
{
    EXPECT_NEAR(tiltAt(0.0f, 0.0f), 0.0f, 0.05f);
    EXPECT_NEAR(tiltAt(30.0f, 0.0f), 30.0f, 0.05f);
    EXPECT_NEAR(tiltAt(-30.0f, 0.0f), 30.0f, 0.05f);
    EXPECT_NEAR(tiltAt(0.0f, 70.0f), 70.0f, 0.05f);
    EXPECT_NEAR(tiltAt(0.0f, -70.0f), 70.0f, 0.05f);
    EXPECT_NEAR(tiltAt(0.0f, 90.0f), 90.0f, 0.05f);
    EXPECT_NEAR(tiltAt(0.0f, 180.0f), 180.0f, 0.05f);
}

TEST(FaceTilt, BothAtOnceCombineAsCosines)
{
    const float expected = std::acos(std::cos(40.0f * kDegToRad)
                                     * std::cos(50.0f * kDegToRad))
                           / kDegToRad;

    EXPECT_NEAR(tiltAt(40.0f, 50.0f), expected, 0.05f);
}

TEST(FaceTilt, GravityIsTakenInAnyUnit)
{
    EXPECT_NEAR(FaceTilt::degrees(0.0f, 0.5f, 0.5f), 45.0f, 0.05f);
    EXPECT_NEAR(FaceTilt::degrees(0.0f, kG, kG), 45.0f, 0.05f);
}

TEST(FaceTilt, GravityThatIsNotADirectionIsFaceDown)
{
    EXPECT_FLOAT_EQ(FaceTilt::degrees(0.0f, 0.0f, 0.0f), 180.0f);
    EXPECT_FLOAT_EQ(FaceTilt::degrees(std::nanf(""), 0.0f, kG), 180.0f);
}

} // namespace
