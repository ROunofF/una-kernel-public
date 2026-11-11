/**
 * Host unit tests for MagEllipsoidFit - the ellipsoid fit that takes soft iron
 * out of a magnetometer tumble.
 *
 * Judged out-of-sample: correct a grid the fit never saw and check that the
 * magnitude no longer depends on direction. Fixtures carry a rotation, because
 * an ellipsoid aligned with the sensor's axes is the easy case.
 */

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "MagEllipsoidFit.hpp"
#include "MagSphereFit.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Ellipsoid {
    double cx;
    double cy;
    double cz;
    double ax;
    double ay;
    double az;
    double yaw   = 0.0;   ///< Rotation of the ellipsoid's own axes about Z,
    double pitch = 0.0;   ///< then about Y.
};

// A point on the ellipsoid surface at the given spherical parameters.
void surfacePoint(const Ellipsoid& e, double theta, double phi, double p[3])
{
    const double local[3] = {
        e.ax * std::sin(theta) * std::cos(phi),
        e.ay * std::sin(theta) * std::sin(phi),
        e.az * std::cos(theta),
    };

    const double cy = std::cos(e.yaw);
    const double sy = std::sin(e.yaw);
    const double cp = std::cos(e.pitch);
    const double sp = std::sin(e.pitch);

    const double r[3][3] = {
        { cy * cp, -sy, cy * sp },
        { sy * cp,  cy, sy * sp },
        { -sp,      0.0, cp     },
    };

    const double centre[3] = { e.cx, e.cy, e.cz };

    for (int i = 0; i < 3; ++i) {
        p[i] = centre[i]
             + r[i][0] * local[0]
             + r[i][1] * local[1]
             + r[i][2] * local[2];
    }
}

void tumble(MagEllipsoidFit& fit, const Ellipsoid& e, int steps,
            double polarFraction = 1.0)
{
    for (int i = 0; i < steps; ++i) {
        for (int j = 0; j < steps; ++j) {
            double p[3];
            surfacePoint(e, polarFraction * kPi * i / (steps - 1),
                         2.0 * kPi * j / steps, p);

            fit.add(static_cast<float>(p[0]),
                    static_cast<float>(p[1]),
                    static_cast<float>(p[2]));
        }
    }
}

// Scatter of the corrected magnitude over an offset grid the fit never saw.
double correctedSpread(const MagEllipsoidFit::Result& r, const Ellipsoid& e)
{
    double sum   = 0.0;
    double sumSq = 0.0;
    int    n     = 0;

    for (int i = 0; i < 37; ++i) {
        for (int j = 0; j < 41; ++j) {
            double p[3];
            surfacePoint(e, kPi * (i + 0.5) / 37.0,
                         2.0 * kPi * (j + 0.5) / 41.0, p);

            const double vx = p[0] - r.offsetX;
            const double vy = p[1] - r.offsetY;
            const double vz = p[2] - r.offsetZ;

            const double cx = r.m00 * vx + r.m01 * vy + r.m02 * vz;
            const double cy = r.m01 * vx + r.m11 * vy + r.m12 * vz;
            const double cz = r.m02 * vx + r.m12 * vy + r.m22 * vz;

            const double magnitude = std::sqrt(cx * cx + cy * cy + cz * cz);

            sum   += magnitude;
            sumSq += magnitude * magnitude;
            ++n;
        }
    }

    const double mean     = sum / n;
    const double variance = (sumSq / n) - (mean * mean);

    return (variance > 0.0) ? std::sqrt(variance) : 0.0;
}

} // namespace

// With no soft iron the fit must not invent any.
TEST(MagEllipsoidFit, ReducesToASphereWhenThereIsNoSoftIron)
{
    const Ellipsoid expected { 12.0, -85.0, 30.0, 48.0, 48.0, 48.0 };

    MagEllipsoidFit fit;
    tumble(fit, expected, 40);

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, 12.0f, 0.01f);
    EXPECT_NEAR(result.offsetY, -85.0f, 0.01f);
    EXPECT_NEAR(result.offsetZ, 30.0f, 0.01f);

    EXPECT_NEAR(result.semiAxis[0], 48.0f, 0.01f);
    EXPECT_NEAR(result.semiAxis[1], 48.0f, 0.01f);
    EXPECT_NEAR(result.semiAxis[2], 48.0f, 0.01f);

    EXPECT_NEAR(result.radius, 48.0f, 0.01f);

    EXPECT_NEAR(result.m00, 1.0f, 0.001f);
    EXPECT_NEAR(result.m11, 1.0f, 0.001f);
    EXPECT_NEAR(result.m22, 1.0f, 0.001f);
    EXPECT_NEAR(result.m01, 0.0f, 0.001f);
    EXPECT_NEAR(result.m02, 0.0f, 0.001f);
    EXPECT_NEAR(result.m12, 0.0f, 0.001f);
}

TEST(MagEllipsoidFit, RecoversCentreAndSemiAxes)
{
    const Ellipsoid expected { 27.0, 90.6, -32.0, 49.5, 45.0, 40.5 };

    MagEllipsoidFit fit;
    tumble(fit, expected, 40);

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, 27.0f, 0.02f);
    EXPECT_NEAR(result.offsetY, 90.6f, 0.02f);
    EXPECT_NEAR(result.offsetZ, -32.0f, 0.02f);

    // Reported longest first.
    EXPECT_NEAR(result.semiAxis[0], 49.5f, 0.02f);
    EXPECT_NEAR(result.semiAxis[1], 45.0f, 0.02f);
    EXPECT_NEAR(result.semiAxis[2], 40.5f, 0.02f);

    // The sphere the correction lands on has the ellipsoid's volume.
    EXPECT_NEAR(result.radius,
                static_cast<float>(std::cbrt(49.5 * 45.0 * 40.5)), 0.02f);
}

// The claim the whole class exists to make.
TEST(MagEllipsoidFit, FlattensTheCorrectedMagnitudeOnFreshSamples)
{
    const Ellipsoid expected { 27.0, 90.6, -32.0, 49.5, 45.0, 40.5 };

    MagEllipsoidFit fit;
    tumble(fit, expected, 40);

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_LT(correctedSpread(result, expected), 0.01);
}

// Axes that do not line up with the sensor's are what off-diagonal terms fix.
TEST(MagEllipsoidFit, HandlesAnEllipsoidThatIsRotated)
{
    const Ellipsoid expected { 5.0, -10.0, 3.0, 60.0, 45.0, 36.0,
                               kPi / 5.0, kPi / 7.0 };

    MagEllipsoidFit fit;
    tumble(fit, expected, 40);

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, 5.0f, 0.05f);
    EXPECT_NEAR(result.offsetY, -10.0f, 0.05f);
    EXPECT_NEAR(result.offsetZ, 3.0f, 0.05f);

    EXPECT_NEAR(result.semiAxis[0], 60.0f, 0.05f);
    EXPECT_NEAR(result.semiAxis[1], 45.0f, 0.05f);
    EXPECT_NEAR(result.semiAxis[2], 36.0f, 0.05f);

    // The rotation can only show up off the diagonal.
    EXPECT_GT(std::fabs(result.m01) + std::fabs(result.m02) +
              std::fabs(result.m12), 0.01f);

    EXPECT_LT(correctedSpread(result, expected), 0.01);
}

// On the same samples a sphere leaves scatter proportional to the anisotropy.
TEST(MagEllipsoidFit, LeavesLessThanASphereDoes)
{
    const Ellipsoid expected { 5.0, -10.0, 3.0, 60.0, 45.0, 36.0,
                               kPi / 5.0, kPi / 7.0 };

    MagEllipsoidFit ellipsoid;
    MagSphereFit    sphere;

    for (int i = 0; i < 40; ++i) {
        for (int j = 0; j < 40; ++j) {
            double p[3];
            surfacePoint(expected, kPi * i / 39.0, 2.0 * kPi * j / 40.0, p);

            ellipsoid.add(static_cast<float>(p[0]),
                          static_cast<float>(p[1]),
                          static_cast<float>(p[2]));
            sphere.add(static_cast<float>(p[0]),
                       static_cast<float>(p[1]),
                       static_cast<float>(p[2]));
        }
    }

    MagEllipsoidFit::Result fitted {};
    ASSERT_TRUE(ellipsoid.solve(fitted));

    MagSphereFit::Result spherical {};
    ASSERT_TRUE(sphere.solve(spherical));

    // A sphere put through a 25% anisotropy sits several uT off it.
    EXPECT_GT(spherical.residualRms, 5.0f);

    EXPECT_LT(correctedSpread(fitted, expected), 0.01);
}

TEST(MagEllipsoidFit, ToleratesNoise)
{
    const Ellipsoid expected { 27.0, 90.6, -32.0, 49.5, 45.0, 40.5 };

    MagEllipsoidFit fit;

    // Deterministic zig-zag on the radius rather than rand(), so no flakes.
    int k = 0;
    for (int i = 0; i < 40; ++i) {
        for (int j = 0; j < 40; ++j) {
            Ellipsoid wobbled = expected;
            const double wobble = ((k++ % 2) == 0) ? 1.005 : 0.995;
            wobbled.ax *= wobble;
            wobbled.ay *= wobble;
            wobbled.az *= wobble;

            double p[3];
            surfacePoint(wobbled, kPi * i / 39.0, 2.0 * kPi * j / 40.0, p);

            fit.add(static_cast<float>(p[0]),
                    static_cast<float>(p[1]),
                    static_cast<float>(p[2]));
        }
    }

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.semiAxis[0], 49.5f, 0.5f);
    EXPECT_NEAR(result.semiAxis[1], 45.0f, 0.5f);
    EXPECT_NEAR(result.semiAxis[2], 40.5f, 0.5f);
}

TEST(MagEllipsoidFit, RejectsTooFewSamples)
{
    MagEllipsoidFit fit;

    for (uint32_t i = 0; i < MagEllipsoidFit::skMinSamples - 1; ++i) {
        fit.add(static_cast<float>(i), static_cast<float>(i * i), 3.0f);
    }

    MagEllipsoidFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

// Rotating about one axis gives a plane, which pins nothing along its normal.
TEST(MagEllipsoidFit, RejectsCoplanarSamples)
{
    MagEllipsoidFit fit;

    for (int i = 0; i < 80; ++i) {
        const double angle = 2.0 * kPi * i / 80.0;
        fit.add(static_cast<float>(50.0 * std::cos(angle)),
                static_cast<float>(50.0 * std::sin(angle)),
                7.0f);
    }

    MagEllipsoidFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

TEST(MagEllipsoidFit, RejectsIdenticalSamples)
{
    MagEllipsoidFit fit;

    for (int i = 0; i < 50; ++i) {
        fit.add(1.0f, 2.0f, 3.0f);
    }

    MagEllipsoidFit::Result result {};
    EXPECT_FALSE(fit.solve(result));
}

TEST(MagEllipsoidFit, ResetClearsEverything)
{
    const Ellipsoid first  { 27.0, 90.6, -32.0, 49.5, 45.0, 40.5 };
    const Ellipsoid second { -3.0, 20.0, -7.0, 52.0, 52.0, 52.0 };

    MagEllipsoidFit fit;
    tumble(fit, first, 20);
    fit.reset();

    EXPECT_EQ(fit.getCount(), 0u);

    tumble(fit, second, 20);

    MagEllipsoidFit::Result result {};
    ASSERT_TRUE(fit.solve(result));

    EXPECT_NEAR(result.offsetX, -3.0f, 0.05f);
    EXPECT_NEAR(result.offsetY, 20.0f, 0.05f);
    EXPECT_NEAR(result.offsetZ, -7.0f, 0.05f);
    EXPECT_NEAR(result.radius, 52.0f, 0.05f);
}
