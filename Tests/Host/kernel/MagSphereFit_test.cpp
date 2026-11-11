/**
 * Host unit tests for MagSphereFit - the least-squares sphere fit that turns a
 * magnetometer tumble into a hard-iron offset (sphere centre) and a field
 * magnitude (sphere radius). Fields are in microtesla, radii near the Earth's.
 */

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "MagSphereFit.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Sphere {
    float cx;
    float cy;
    float cz;
    float r;
};

// Cover the sphere with a latitude/longitude grid. `polarFraction` < 1 samples
// only a cap around the +Z pole, which is what a half-hearted rotation gives.
void tumble(MagSphereFit& fit, const Sphere& s, int steps,
            double polarFraction = 1.0)
{
    for (int i = 0; i < steps; ++i) {
        for (int j = 0; j < steps; ++j) {
            const double theta = polarFraction * kPi * i / (steps - 1);
            const double phi   = 2.0 * kPi * j / steps;

            fit.add(static_cast<float>(s.cx + s.r * std::sin(theta) * std::cos(phi)),
                    static_cast<float>(s.cy + s.r * std::sin(theta) * std::sin(phi)),
                    static_cast<float>(s.cz + s.r * std::cos(theta)));
        }
    }
}

} // namespace

TEST(MagSphereFit, RecoversCentreAndRadiusFromAFullTumble)
{
    const Sphere expected { 12.0f, -85.0f, 30.0f, 48.0f };

    MagSphereFit fit;
    tumble(fit, expected, 40);

    MagSphereFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, expected.cx, 0.01f);
    EXPECT_NEAR(result.offsetY, expected.cy, 0.01f);
    EXPECT_NEAR(result.offsetZ, expected.cz, 0.01f);
    EXPECT_NEAR(result.radius,  expected.r,  0.01f);
    EXPECT_NEAR(result.residualRms, 0.0f, 0.01f);
}

// With no iron near the sensor the fit must not invent an offset.
TEST(MagSphereFit, RecoversAZeroOffset)
{
    const Sphere expected { 0.0f, 0.0f, 0.0f, 50.0f };

    MagSphereFit fit;
    tumble(fit, expected, 30);

    MagSphereFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, 0.0f, 0.01f);
    EXPECT_NEAR(result.offsetY, 0.0f, 0.01f);
    EXPECT_NEAR(result.offsetZ, 0.0f, 0.01f);
    EXPECT_NEAR(result.radius, expected.r, 0.01f);
}

// The RMS distance from the surface tracks the noise, not field or offset.
TEST(MagSphereFit, ResidualReportsTheScatterAndNotTheOffset)
{
    const Sphere expected { 12.0f, -85.0f, 30.0f, 48.0f };
    constexpr float kNoise = 1.0f;

    MagSphereFit fit;

    // Deterministic zig-zag rather than rand(), so the test cannot flake.
    int k = 0;
    for (int i = 0; i < 40; ++i) {
        for (int j = 0; j < 40; ++j) {
            const double theta  = kPi * i / 39.0;
            const double phi    = 2.0 * kPi * j / 40.0;
            const double wobble = ((k++ % 2) == 0) ? kNoise : -kNoise;
            const double radius = expected.r + wobble;

            fit.add(static_cast<float>(expected.cx + radius * std::sin(theta) * std::cos(phi)),
                    static_cast<float>(expected.cy + radius * std::sin(theta) * std::sin(phi)),
                    static_cast<float>(expected.cz + radius * std::cos(theta)));
        }
    }

    MagSphereFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.radius, expected.r, 0.05f);
    EXPECT_NEAR(result.residualRms, kNoise, 0.05f);
}

// A covered sphere spans about 2r on every axis.
TEST(MagSphereFit, SpansReportTheCoverageOfTheRotation)
{
    const Sphere expected { 12.0f, -85.0f, 30.0f, 48.0f };

    MagSphereFit full;
    tumble(full, expected, 40);

    EXPECT_NEAR(full.getSpanX(), 2 * expected.r, 1.0f);
    EXPECT_NEAR(full.getSpanY(), 2 * expected.r, 1.0f);
    EXPECT_NEAR(full.getSpanZ(), 2 * expected.r, 1.0f);

    // A cap around the pole barely moves Z: the device was never turned over.
    MagSphereFit cap;
    tumble(cap, expected, 12, 0.35);

    EXPECT_LT(cap.getSpanZ(), 0.5f * full.getSpanZ());
}

TEST(MagSphereFit, RejectsTooFewSamples)
{
    MagSphereFit fit;

    for (uint32_t i = 0; i < MagSphereFit::skMinSamples - 1; ++i) {
        fit.add(static_cast<float>(i), static_cast<float>(i * i), 3.0f);
    }

    MagSphereFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

// An unmoved or stuck sensor carries no sphere; refusing beats inventing one.
TEST(MagSphereFit, RejectsIdenticalSamples)
{
    MagSphereFit fit;

    for (int i = 0; i < 50; ++i) {
        fit.add(1.0f, 2.0f, 3.0f);
    }

    MagSphereFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

// One-axis rotation gives a plane, leaving the centre free along its normal.
TEST(MagSphereFit, RejectsCoplanarSamples)
{
    MagSphereFit fit;

    for (int i = 0; i < 36; ++i) {
        const double angle = 2.0 * kPi * i / 36.0;
        fit.add(static_cast<float>(50.0 * std::cos(angle)),
                static_cast<float>(50.0 * std::sin(angle)),
                7.0f);
    }

    MagSphereFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

TEST(MagSphereFit, ResetClearsEverything)
{
    const Sphere first  { 12.0f, -85.0f, 30.0f, 48.0f };
    const Sphere second { -3.0f,  20.0f, -7.0f, 52.0f };

    MagSphereFit fit;
    tumble(fit, first, 20);
    fit.reset();

    EXPECT_EQ(fit.getCount(), 0u);
    EXPECT_FLOAT_EQ(fit.getSpanX(), 0.0f);

    tumble(fit, second, 20);

    MagSphereFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, second.cx, 0.01f);
    EXPECT_NEAR(result.offsetY, second.cy, 0.01f);
    EXPECT_NEAR(result.offsetZ, second.cz, 0.01f);
    EXPECT_NEAR(result.radius,  second.r,  0.01f);
}
