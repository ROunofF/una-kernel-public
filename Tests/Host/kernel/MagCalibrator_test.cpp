/**
 * Host unit tests for MagCalibrator and MagCalibrationCheck - a calibration run
 * end to end, without a chip: the calibrator fits a tumble, the check judges
 * the result on a second tumble it never saw.
 */

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "MagCalibrator.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Ellipsoid {
    double cx;
    double cy;
    double cz;
    double ax;
    double ay;
    double az;
    double yaw   = 0.0;
    double pitch = 0.0;
};

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

// `offset` shifts the grid, so a second tumble hits points the first did not.
void feed(MagCalibrator& calibrator, const Ellipsoid& e, int steps,
          double offset = 0.0, double polarFraction = 1.0)
{
    for (int i = 0; i < steps; ++i) {
        for (int j = 0; j < steps; ++j) {
            double p[3];
            surfacePoint(e, polarFraction * kPi * (i + offset) / (steps - 1),
                         2.0 * kPi * (j + offset) / steps, p);

            calibrator.add(static_cast<float>(p[0]),
                           static_cast<float>(p[1]),
                           static_cast<float>(p[2]));
        }
    }
}

void feed(MagCalibrationCheck& check, const Ellipsoid& e, int steps,
          double offset = 0.0)
{
    for (int i = 0; i < steps; ++i) {
        for (int j = 0; j < steps; ++j) {
            double p[3];
            surfacePoint(e, kPi * (i + offset) / (steps - 1),
                         2.0 * kPi * (j + offset) / steps, p);

            check.add(static_cast<float>(p[0]),
                      static_cast<float>(p[1]),
                      static_cast<float>(p[2]));
        }
    }
}

// A hard-iron offset larger than the field, and enough soft iron to matter.
const Ellipsoid kWatch { 27.0, 90.6, -32.0, 49.5, 45.0, 40.5,
                         kPi / 6.0, kPi / 9.0 };

const double kCapCentre[3] = { 27.0, 90.6, -32.0 };
const double kTilted[3]    = { 0.57735026919, 0.57735026919, 0.57735026919 };

} // namespace

TEST(MagCalibrator, FitsATumbleAndAcceptsIt)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    EXPECT_EQ(calibrator.getCoverage(), MagCalibrator::skDirections);

    EXPECT_NEAR(record.offsetX, 27.0f, 0.1f);
    EXPECT_NEAR(record.offsetY, 90.6f, 0.1f);
    EXPECT_NEAR(record.offsetZ, -32.0f, 0.1f);

    EXPECT_NEAR(record.radius,
                static_cast<float>(std::cbrt(49.5 * 45.0 * 40.5)), 0.1f);

    EXPECT_EQ(record.samples, 900u);
}

TEST(MagCalibrator, TheResultHoldsOnASecondTumble)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    MagCalibrationCheck check;
    ASSERT_TRUE(check.start(record));
    feed(check, kWatch, 27, 0.5);

    MagCalibrationCheck::Result result;
    ASSERT_TRUE(check.getResult(result));

    EXPECT_NEAR(result.mean, record.radius, 0.1f);
    EXPECT_LT(result.spread, 0.1f);
    EXPECT_TRUE(check.passed(calibrator.getLimits(), 100));
}

// A calibration fitted to one device must not pass on a different one.
TEST(MagCalibrator, TheCheckFailsForTheWrongCalibration)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    const Ellipsoid other { -40.0, 12.0, 55.0, 52.0, 44.0, 38.0, 0.0, 0.0 };

    MagCalibrationCheck check;
    ASSERT_TRUE(check.start(record));
    feed(check, other, 27, 0.5);

    EXPECT_FALSE(check.passed(calibrator.getLimits(), 100));
}

// A correction applied to a field half the size it was fitted in gives a
// steady, wrong magnitude. The spread assertion shows the scatter bound alone
// would pass it, so the refusal has to come from the level.
TEST(MagCalibrationCheck, FailsWhenTheLevelIsSteadyAndWrong)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    // Same centre and proportions; only the field is halved.
    Ellipsoid weak = kWatch;
    weak.ax *= 0.5;
    weak.ay *= 0.5;
    weak.az *= 0.5;

    MagCalibrationCheck check;
    ASSERT_TRUE(check.start(record));
    feed(check, weak, 27, 0.5);

    MagCalibrationCheck::Result result;
    ASSERT_TRUE(check.getResult(result));

    const float allowed =
            calibrator.getLimits().maxCorrectedSpreadFactor * record.radius;

    EXPECT_LT(result.spread, allowed);

    EXPECT_GT(std::fabs(result.mean - record.radius), allowed);

    EXPECT_FALSE(check.passed(calibrator.getLimits(), 100));
}

namespace {

/// A tumble confined to a cap of half-angle capDeg about `axis`, as a wrist
/// rocked without turning over; an alternating error stands in for noise.
void feedCap(MagCalibrator& calibrator, const double axis[3], double capDeg,
             double noise)
{
    const double t[3] = { std::fabs(axis[0]) < 0.9 ? 1.0 : 0.0,
                          std::fabs(axis[0]) < 0.9 ? 0.0 : 1.0,
                          0.0 };

    double e1[3] = { axis[1] * t[2] - axis[2] * t[1],
                     axis[2] * t[0] - axis[0] * t[2],
                     axis[0] * t[1] - axis[1] * t[0] };

    const double n1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    for (double& v : e1) {
        v /= n1;
    }

    const double e2[3] = { axis[1] * e1[2] - axis[2] * e1[1],
                           axis[2] * e1[0] - axis[0] * e1[2],
                           axis[0] * e1[1] - axis[1] * e1[0] };

    int k = 0;

    for (int i = 1; i <= 25; ++i) {
        const double polar = (capDeg * kPi / 180.0) * i / 25.0;

        for (int j = 0; j < 30; ++j, ++k) {
            const double phi  = 2.0 * kPi * j / 30.0;
            const double sign = (k % 2 == 0) ? noise : -noise;

            float s[3];
            for (int a = 0; a < 3; ++a) {
                const double d = std::cos(polar) * axis[a] +
                        std::sin(polar) * (std::cos(phi) * e1[a] +
                                           std::sin(phi) * e2[a]);

                s[a] = static_cast<float>(kCapCentre[a] + 45.0 * d +
                                          ((a == k % 3) ? sign : -sign));
            }

            calibrator.add(s[0], s[1], s[2]);
        }
    }
}

} // namespace

// Every sensor axis still moves widely because the cap is tilted, but most
// directions are never reached.
TEST(MagCalibrator, RefusesAShallowTumbleAboutATiltedAxis)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feedCap(calibrator, kTilted, 45.0, 0.5);

    MagCalibration::Record record;
    EXPECT_EQ(calibrator.finish(record),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
    EXPECT_LT(calibrator.getCoverage(),
              calibrator.getLimits().minCoveredDirections);
}

// Its centre can still sit a few microtesla off once samples are noisy.
TEST(MagCalibrator, RefusesAHemisphere)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feedCap(calibrator, kTilted, 90.0, 0.5);

    MagCalibration::Record record;
    EXPECT_EQ(calibrator.finish(record),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
}

TEST(MagCalibrator, AcceptsTwoThirdsOfTheSphereAboutATiltedAxis)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feedCap(calibrator, kTilted, 125.0, 0.5);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    EXPECT_GE(calibrator.getCoverage(),
              calibrator.getLimits().minCoveredDirections);
    EXPECT_NEAR(record.offsetX, kCapCentre[0], 1.0);
    EXPECT_NEAR(record.offsetY, kCapCentre[1], 1.0);
    EXPECT_NEAR(record.offsetZ, kCapCentre[2], 1.0);
}

// From the centre of a still watch's noise every direction is reached; nobody
// turned it, so the verdict is about rotation, not the field.
TEST(MagCalibrator, CallsAStillWatchNarrowRatherThanBlamingTheField)
{
    MagCalibrator calibrator;
    calibrator.start(0);

    for (uint32_t i = 0; i < 400; ++i) {
        // All 26 directions in an odd cycle, 0.4 uT from where the field sits.
        const uint32_t step = (i % 27U) % 26U;
        const uint32_t code = step + ((step >= 13U) ? 1U : 0U);

        const int a = static_cast<int>(code / 9U) - 1;
        const int b = static_cast<int>((code / 3U) % 3U) - 1;
        const int c = static_cast<int>(code % 3U) - 1;

        const double scale =
                0.4 / std::sqrt(static_cast<double>(a * a + b * b + c * c));

        calibrator.add(static_cast<float>(kCapCentre[0] + scale * a),
                       static_cast<float>(kCapCentre[1] + scale * b),
                       static_cast<float>(kCapCentre[2] + scale * c));
    }

    MagCalibration::Record record;
    EXPECT_EQ(calibrator.finish(record),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
    EXPECT_EQ(calibrator.getCoverage(), 0u);
}

// With no expected length, the samples kept are thinned as the run grows.
TEST(MagCalibrator, ALongRunKeepsItsCoverage)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 60);   // 3600 samples

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);
    EXPECT_EQ(calibrator.getCoverage(), MagCalibrator::skDirections);
}

namespace {

constexpr uint32_t kExpected = 512;

/// Six samples in a row at every direction.
constexpr uint32_t kPerDirection = 6;

void addDirection(MagCalibrator& calibrator, int a, int b, int c)
{
    const double scale =
            45.0 / std::sqrt(static_cast<double>(a * a + b * b + c * c));

    calibrator.add(static_cast<float>(kCapCentre[0] + scale * a),
                   static_cast<float>(kCapCentre[1] + scale * b),
                   static_cast<float>(kCapCentre[2] + scale * c));
}

/// Exactly kExpected samples: one direction held, then every direction.
void feedExpected(MagCalibrator& calibrator)
{
    const uint32_t held =
            kExpected - (MagCalibrator::skDirections * kPerDirection);

    for (uint32_t i = 0; i < held; ++i) {
        addDirection(calibrator, -1, -1, -1);
    }

    for (int a = -1; a <= 1; ++a) {
        for (int b = -1; b <= 1; ++b) {
            for (int c = -1; c <= 1; ++c) {
                if ((a == 0) && (b == 0) && (c == 0)) {
                    continue;
                }

                for (uint32_t k = 0; k < kPerDirection; ++k) {
                    addDirection(calibrator, a, b, c);
                }
            }
        }
    }
}

} // namespace

// Thinning the whole run by half at its last sample would leave most of the
// directions reached one kept sample each.
TEST(MagCalibrator, OneSampleMoreThanExpectedTakesNoDirectionAway)
{
    MagCalibrator exact;
    exact.start(kExpected);
    feedExpected(exact);

    MagCalibrator over;
    over.start(kExpected);
    feedExpected(over);
    addDirection(over, -1, -1, -1);

    MagCalibration::Record record;
    (void) exact.finish(record);
    (void) over.finish(record);

    EXPECT_EQ(exact.getCoverage(), MagCalibrator::skDirections);
    EXPECT_EQ(over.getCoverage(), MagCalibrator::skDirections);
}

// The two failures ask different things of the wearer: keep collecting, or
// turn the watch differently.
TEST(MagCalibrator, TellsTooFewSamplesApartFromTooNarrowATumble)
{
    // A wide tumble, far too short.
    MagCalibrator brief;
    brief.start(0);

    for (int i = 0; i < 5; ++i) {
        double p[3];
        surfacePoint(kWatch, kPi * i / 4.0, 2.0 * kPi * i / 5.0, p);
        brief.add(static_cast<float>(p[0]),
                  static_cast<float>(p[1]),
                  static_cast<float>(p[2]));
    }

    MagCalibration::Record record;
    EXPECT_EQ(brief.finish(record), MagCalibration::Verdict::TOO_FEW_SAMPLES);

    // Plenty of samples, but all in a narrow band.
    MagCalibrator band;
    band.start(0);

    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 30; ++j) {
            double p[3];
            surfacePoint(kWatch, kPi * (0.48 + 0.04 * i / 19.0),
                         2.0 * kPi * j / 30.0, p);
            band.add(static_cast<float>(p[0]),
                     static_cast<float>(p[1]),
                     static_cast<float>(p[2]));
        }
    }

    EXPECT_EQ(band.finish(record),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
}

// Enough to fit but under the limit: the record is still filled for reporting.
TEST(MagCalibrator, FillsTheRecordWhenItRejectsForSampleCount)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 10);

    MagCalibration::Record record;
    ASSERT_LT(calibrator.getProgress().samples,
              calibrator.getLimits().minSamples);

    EXPECT_EQ(calibrator.finish(record),
              MagCalibration::Verdict::TOO_FEW_SAMPLES);

    EXPECT_GT(record.radius, 0.0f);
    EXPECT_EQ(record.samples, 100u);
}

TEST(MagCalibrator, RejectsDegenerateSamples)
{
    MagCalibrator calibrator;
    calibrator.start(0);

    for (int i = 0; i < 500; ++i) {
        calibrator.add(1.0f, 2.0f, 3.0f);
    }

    MagCalibration::Record record;
    EXPECT_EQ(calibrator.finish(record),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
    EXPECT_EQ(calibrator.getCoverage(), 0u);
}

TEST(MagCalibrator, ProgressTracksTheTumble)
{
    MagCalibrator calibrator;
    calibrator.start(0);

    EXPECT_EQ(calibrator.getProgress().samples, 0u);

    feed(calibrator, kWatch, 30);

    const MagCalibrator::Progress progress = calibrator.getProgress();

    EXPECT_EQ(progress.samples, 900u);

    // A covered sphere moves every axis by about twice the field.
    EXPECT_GT(progress.spanX, 60.0f);
    EXPECT_GT(progress.spanY, 60.0f);
    EXPECT_GT(progress.spanZ, 60.0f);
}

TEST(MagCalibrator, StartDiscardsThePreviousRun)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 20);

    calibrator.start(0);
    EXPECT_EQ(calibrator.getProgress().samples, 0u);

    const Ellipsoid other { 0.0, 0.0, 0.0, 50.0, 50.0, 50.0 };
    feed(calibrator, other, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    EXPECT_NEAR(record.offsetX, 0.0f, 0.1f);
    EXPECT_NEAR(record.offsetY, 0.0f, 0.1f);
    EXPECT_NEAR(record.offsetZ, 0.0f, 0.1f);
    EXPECT_NEAR(record.radius, 50.0f, 0.1f);
}

// Before a start is taken it counts nothing and cannot pass, however flat
// the readings look.
TEST(MagCalibrationCheck, AnUnstartedCheckCountsNothingAndDoesNotPass)
{
    MagCalibrationCheck check;

    feed(check, kWatch, 27, 0.5);

    MagCalibrationCheck::Result result;
    EXPECT_FALSE(check.getResult(result));
    EXPECT_EQ(check.getCount(), 0u);

    MagCalibration::Limits limits;
    EXPECT_FALSE(check.passed(limits, 100));
}

TEST(MagCalibrationCheck, FailsOnTooFewSamples)
{
    MagCalibrator calibrator;
    calibrator.start(0);
    feed(calibrator, kWatch, 30);

    MagCalibration::Record record;
    ASSERT_EQ(calibrator.finish(record), MagCalibration::Verdict::OK);

    MagCalibrationCheck check;
    ASSERT_TRUE(check.start(record));
    feed(check, kWatch, 5, 0.5);

    EXPECT_LT(check.getCount(), 100u);
    EXPECT_FALSE(check.passed(calibrator.getLimits(), 100));
}
