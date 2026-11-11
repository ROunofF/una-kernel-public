/**
 * Host unit tests for MagCalibration - the record a calibrated device carries
 * and the policy that decides whether a fit may become one. Fields are in
 * microtesla; the fixture has a hard-iron offset larger than the field.
 */

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "MagCalibration.hpp"

namespace {

// A fit that should pass: 750 samples, a covered sphere, a small residual.
MagCalibration::Record goodRecord()
{
    MagCalibration::Record record;

    record.offsetX  = 19.2f;
    record.offsetY  = 90.7f;
    record.offsetZ  = -30.5f;
    record.radius   = 44.3f;
    record.residual = 1.0f;
    record.samples  = 750;

    return record;
}

constexpr uint8_t kAllDirections = 26;

const MagCalibration::Limits kDefaults {};

} // namespace

TEST(MagCalibration, AcceptsAGoodFit)
{
    EXPECT_EQ(MagCalibration::evaluate(goodRecord(), kAllDirections, kDefaults),
              MagCalibration::Verdict::OK);
}

TEST(MagCalibration, RejectsAShortSweep)
{
    MagCalibration::Record record = goodRecord();
    record.samples = 100;

    EXPECT_EQ(MagCalibration::evaluate(record, kAllDirections, kDefaults),
              MagCalibration::Verdict::TOO_FEW_SAMPLES);
}

// Twenty of twenty-six directions is the least a tumble may reach.
TEST(MagCalibration, AcceptsTheLeastCoverageAllowedAndNoLess)
{
    EXPECT_EQ(MagCalibration::evaluate(goodRecord(), 20, kDefaults),
              MagCalibration::Verdict::OK);
    EXPECT_EQ(MagCalibration::evaluate(goodRecord(), 19, kDefaults),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
}

TEST(MagCalibration, RejectsSamplesThatAreNotOnASphere)
{
    MagCalibration::Record record = goodRecord();

    // Far past the few percent of soft iron the ellipsoid fit removes.
    record.residual = 0.35f * record.radius;

    EXPECT_EQ(MagCalibration::evaluate(record, kAllDirections, kDefaults),
              MagCalibration::Verdict::RESIDUAL_TOO_HIGH);
}

TEST(MagCalibration, RejectsAnImplausibleRadius)
{
    MagCalibration::Record small = goodRecord();
    small.radius = 5.0f;

    EXPECT_EQ(MagCalibration::evaluate(small, kAllDirections, kDefaults),
              MagCalibration::Verdict::RADIUS_OUT_OF_RANGE);

    MagCalibration::Record large = goodRecord();
    large.radius = 500.0f;

    EXPECT_EQ(MagCalibration::evaluate(large, kAllDirections, kDefaults),
              MagCalibration::Verdict::RADIUS_OUT_OF_RANGE);
}

// A turn about one axis still fits a radius, and it is nonsense. Judged on
// coverage, which needs no radius, the tumble is too narrow.
TEST(MagCalibration, CallsAFlatTumbleNarrowRatherThanBlamingTheField)
{
    MagCalibration::Record record = goodRecord();
    record.radius = 10.45f;   // a flat spin fits to a radius like this

    EXPECT_EQ(MagCalibration::evaluate(record, 8, kDefaults),
              MagCalibration::Verdict::COVERAGE_TOO_NARROW);
}

// The reverse: a full tumble in a weak field blames the field.
TEST(MagCalibration, StillBlamesAWeakFieldThatWasTumbledFully)
{
    MagCalibration::Record record = goodRecord();
    record.radius = 10.0f;

    EXPECT_EQ(MagCalibration::evaluate(record, kAllDirections, kDefaults),
              MagCalibration::Verdict::RADIUS_OUT_OF_RANGE);
}

TEST(MagCalibration, LimitsAreTightenable)
{
    MagCalibration::Limits factory;
    factory.minRadius = 48.0f;
    factory.maxRadius = 53.0f;

    EXPECT_EQ(MagCalibration::evaluate(goodRecord(), kAllDirections, factory),
              MagCalibration::Verdict::RADIUS_OUT_OF_RANGE);

    MagCalibration::Record outdoors = goodRecord();
    outdoors.radius = 50.4f;

    EXPECT_EQ(MagCalibration::evaluate(outdoors, kAllDirections, factory),
              MagCalibration::Verdict::OK);
}

// Judged and applied by one rule: a fit that clears every bound but is not a
// record set() would take is not accepted.
TEST(MagCalibration, DoesNotAcceptARecordTheCorrectionWouldRefuse)
{
    MagCalibration::Record nan = goodRecord();
    nan.residual = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(MagCalibration::evaluate(nan, kAllDirections, kDefaults),
              MagCalibration::Verdict::NOT_USABLE);

    MagCalibration::Record negative = goodRecord();
    negative.residual = -1.0f;
    EXPECT_EQ(MagCalibration::evaluate(negative, kAllDirections, kDefaults),
              MagCalibration::Verdict::NOT_USABLE);

    MagCalibration::Record indefinite = goodRecord();
    indefinite.m01 = 2.0f;
    EXPECT_EQ(MagCalibration::evaluate(indefinite, kAllDirections, kDefaults),
              MagCalibration::Verdict::NOT_USABLE);
}

// Everything reading the radius scales a tolerance by it, so a record without
// a usable one is unreadable, not weak.
TEST(MagCalibration, RefusesARecordWithoutAUsableRadius)
{
    MagCalibration calibration;

    MagCalibration::Record record;  // identity matrix, zero offsets

    record.radius = 0.0f;
    EXPECT_FALSE(calibration.set(record));

    record.radius = -48.0f;
    EXPECT_FALSE(calibration.set(record));

    record.radius = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(calibration.set(record));

    // A NaN residual passes every bound by failing every comparison.
    record.radius   = 48.0f;
    record.residual = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(calibration.set(record));

    record.residual = 1.0f;
    EXPECT_TRUE(calibration.set(record));
}

TEST(MagCalibration, CorrectsNothingUntilOneIsHeld)
{
    MagCalibration calibration;
    ASSERT_FALSE(calibration.isValid());

    float x = 19.2f;
    float y = 90.7f;
    float z = -30.5f;

    calibration.apply(x, y, z);

    EXPECT_FLOAT_EQ(x, 19.2f);
    EXPECT_FLOAT_EQ(y, 90.7f);
    EXPECT_FLOAT_EQ(z, -30.5f);
}

// Without the offset the magnitude is the field, not the field plus the iron.
TEST(MagCalibration, SubtractsTheOffset)
{
    MagCalibration calibration;
    ASSERT_TRUE(calibration.set(goodRecord()));
    ASSERT_TRUE(calibration.isValid());

    // A sample taken with the field pointing along +X.
    float x = 19.2f + 44.3f;
    float y = 90.7f;
    float z = -30.5f;

    calibration.apply(x, y, z);

    EXPECT_NEAR(x, 44.3f, 0.001f);
    EXPECT_NEAR(y, 0.0f, 0.001f);
    EXPECT_NEAR(z, 0.0f, 0.001f);

    const float magnitude = std::sqrt(x * x + y * y + z * z);
    EXPECT_NEAR(magnitude, 44.3f, 0.001f);
}

TEST(MagCalibration, AppliesTheMatrixAfterTheOffset)
{
    MagCalibration::Record record = goodRecord();
    record.m11 = 2.0f;

    MagCalibration calibration;
    ASSERT_TRUE(calibration.set(record));

    float x = 19.2f;
    float y = 90.7f + 10.0f;
    float z = -30.5f;

    calibration.apply(x, y, z);

    EXPECT_NEAR(x, 0.0f, 0.001f);
    EXPECT_NEAR(y, 20.0f, 0.001f);
    EXPECT_NEAR(z, 0.0f, 0.001f);
}

// Off-diagonal terms shear, undoing an ellipsoid not aligned with the sensor.
TEST(MagCalibration, AppliesTheOffDiagonalTerms)
{
    MagCalibration::Record record = goodRecord();
    record.m01 = 0.5f;

    MagCalibration calibration;
    ASSERT_TRUE(calibration.set(record));

    float x = 19.2f + 2.0f;
    float y = 90.7f + 4.0f;
    float z = -30.5f;

    calibration.apply(x, y, z);

    EXPECT_NEAR(x, 2.0f + 0.5f * 4.0f, 0.001f);
    EXPECT_NEAR(y, 0.5f * 2.0f + 4.0f, 0.001f);
    EXPECT_NEAR(z, 0.0f, 0.001f);
}

// A NaN or a zero scale would turn every later sample into garbage that looks
// like data.
TEST(MagCalibration, RefusesACorruptRecord)
{
    MagCalibration calibration;

    MagCalibration::Record nan = goodRecord();
    nan.offsetX = std::nanf("");
    EXPECT_FALSE(calibration.set(nan));
    EXPECT_FALSE(calibration.isValid());

    MagCalibration::Record infinite = goodRecord();
    infinite.offsetZ = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(calibration.set(infinite));

    // An RMS distance cannot be negative.
    MagCalibration::Record negative = goodRecord();
    negative.residual = -1.0f;
    EXPECT_FALSE(calibration.set(negative));

    // Not positive definite: a zero on the diagonal collapses an axis.
    MagCalibration::Record collapsed = goodRecord();
    collapsed.m11 = 0.0f;
    EXPECT_FALSE(calibration.set(collapsed));

    // Nor is a negative one, which would mirror an axis.
    MagCalibration::Record mirrored = goodRecord();
    mirrored.m22 = -1.0f;
    EXPECT_FALSE(calibration.set(mirrored));

    // Positive diagonal, negative determinant: a saddle, not a correction.
    MagCalibration::Record indefinite = goodRecord();
    indefinite.m01 = 2.0f;
    EXPECT_FALSE(calibration.set(indefinite));
}

// A failed attempt leaves the previous calibration in force.
TEST(MagCalibration, KeepsTheOldRecordWhenANewOneIsRefused)
{
    MagCalibration calibration;
    ASSERT_TRUE(calibration.set(goodRecord()));

    MagCalibration::Record broken = goodRecord();
    broken.m00 = 0.0f;

    EXPECT_FALSE(calibration.set(broken));
    ASSERT_TRUE(calibration.isValid());
    EXPECT_FLOAT_EQ(calibration.get().offsetY, goodRecord().offsetY);
}

TEST(MagCalibration, ClearStopsCorrecting)
{
    MagCalibration calibration;
    ASSERT_TRUE(calibration.set(goodRecord()));

    calibration.clear();
    EXPECT_FALSE(calibration.isValid());

    float x = 19.2f;
    float y = 90.7f;
    float z = -30.5f;

    calibration.apply(x, y, z);

    EXPECT_FLOAT_EQ(y, 90.7f);
}
