/**
 ******************************************************************************
 * @file    MagCalibrationSession_test.cpp
 * @brief   Host tests for one calibration or verification run: what it
 *          concludes from what it was fed, and what it refuses. Plain calls,
 *          no thread or clock; the fits are covered by MagCalibrator_test.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "MagCalibrationSession.hpp"

namespace {

using Mode = MagCalibrationSession::Mode;

/// A tumble reaching all 26 directions on a sphere of radius 50. The cycle is
/// an odd 27 samples so it survives the calibrator thinning its samples.
void tumble(MagCalibrationSession& session, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++) {
        // 0..26 over {-1,0,1}^3, skipping 13, which is the centre.
        const uint32_t step = (i % 27U) % 26U;
        const uint32_t code = step + ((step >= 13U) ? 1U : 0U);

        const int a = static_cast<int>(code / 9U) - 1;
        const int b = static_cast<int>((code / 3U) % 3U) - 1;
        const int c = static_cast<int>(code % 3U) - 1;

        const float scale =
                50.0f / std::sqrt(static_cast<float>(a * a + b * b + c * c));

        session.feed(scale * a, scale * b, scale * c);
    }
}

MagCalibration::Record identity()
{
    MagCalibration::Record record;
    record.radius = 50.0f;
    return record;
}

} // namespace

// -----------------------------------------------------------------------------
// FIT
// -----------------------------------------------------------------------------

TEST(MagCalSession, ACompletedFitCarriesItsVerdictRecordAndModel)
{
    MagCalibrationSession session;

    session.startFit(0);
    tumble(session, 520);

    const MagCalibrationSession::Result result = session.finish(true);

    EXPECT_EQ(result.mode, Mode::FIT);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.verdict, MagCalibration::Verdict::OK)
            << "verdict " << static_cast<unsigned>(result.verdict);
    EXPECT_GT(result.record.radius, 0.0f);
    EXPECT_EQ(result.progress.samples, 520u);
    EXPECT_TRUE(result.haveSphere);
    EXPECT_EQ(result.coverage, MagCalibrator::skDirections);
    EXPECT_FALSE(result.haveCheck);
    EXPECT_TRUE(result.fitAccepted());
}

// A run nobody fed still ends, and says why it has nothing.
TEST(MagCalSession, AStarvedFitReportsTooFewSamples)
{
    MagCalibrationSession session;

    session.startFit(0);

    const MagCalibrationSession::Result result = session.finish(true);

    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.verdict, MagCalibration::Verdict::TOO_FEW_SAMPLES);
    EXPECT_FALSE(result.haveSphere);
}

// A run that fitted nothing must not report the previous run's record.
TEST(MagCalSession, AFitThatFoundNothingDoesNotReportThePreviousOne)
{
    MagCalibrationSession session;

    session.startFit(0);
    tumble(session, 520);
    ASSERT_EQ(session.finish(true).verdict, MagCalibration::Verdict::OK);

    session.startFit(0);
    tumble(session, 3);

    const MagCalibrationSession::Result after = session.finish(true);

    EXPECT_NE(after.verdict, MagCalibration::Verdict::OK);
    EXPECT_FLOAT_EQ(after.record.radius, 0.0f);
    EXPECT_FLOAT_EQ(after.record.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(after.record.m00, 1.0f);
    EXPECT_EQ(after.record.samples, 0u);
    EXPECT_FALSE(after.haveSphere);
    EXPECT_EQ(after.coverage, 0u);
    EXPECT_EQ(after.progress.samples, 3u);
}

// A run ended early concludes nothing, however much it had collected.
TEST(MagCalSession, AnUncompletedRunConcludesNothing)
{
    MagCalibrationSession session;

    session.startFit(0);
    tumble(session, 520);

    const MagCalibrationSession::Result result = session.finish(false);

    EXPECT_EQ(result.mode, Mode::FIT);
    EXPECT_FALSE(result.completed);
    EXPECT_EQ(result.verdict, MagCalibration::Verdict::TOO_FEW_SAMPLES);
    EXPECT_FLOAT_EQ(result.record.radius, 0.0f);
    EXPECT_FALSE(result.haveSphere);
    EXPECT_EQ(result.coverage, 0u);
}

// Each condition on its own: a session's defaults already make an early or a
// check result carry a verdict that is not OK, so only this can tell them apart.
TEST(MagCalSession, OnlyACompletedFitWithAnOkVerdictIsKept)
{
    MagCalibrationSession::Result kept;
    kept.mode      = Mode::FIT;
    kept.completed = true;
    kept.verdict   = MagCalibration::Verdict::OK;
    EXPECT_TRUE(kept.fitAccepted());

    MagCalibrationSession::Result early = kept;
    early.completed = false;
    EXPECT_FALSE(early.fitAccepted());

    MagCalibrationSession::Result check = kept;
    check.mode = Mode::CHECK;
    EXPECT_FALSE(check.fitAccepted());

    MagCalibrationSession::Result rejected = kept;
    rejected.verdict = MagCalibration::Verdict::COVERAGE_TOO_NARROW;
    EXPECT_FALSE(rejected.fitAccepted());
}

// -----------------------------------------------------------------------------
// CHECK
// -----------------------------------------------------------------------------

TEST(MagCalSession, ACheckNeedsACorrectionItCanApply)
{
    MagCalibrationSession session;

    // A matrix that would collapse every axis rather than correct one, in an
    // otherwise usable record.
    MagCalibration::Record broken = identity();
    broken.m00 = 0.0f;
    broken.m11 = 0.0f;
    broken.m22 = 0.0f;

    EXPECT_FALSE(session.startCheck(broken));
}

// The tumble is already a sphere of the right radius, so identity holds.
TEST(MagCalSession, ACompletedCheckCarriesItsMeasurement)
{
    MagCalibrationSession session;

    ASSERT_TRUE(session.startCheck(identity()));
    tumble(session, 220);

    const MagCalibrationSession::Result result = session.finish(true);

    EXPECT_EQ(result.mode, Mode::CHECK);
    EXPECT_TRUE(result.completed);
    ASSERT_TRUE(result.haveCheck);
    EXPECT_NEAR(result.check.mean, 50.0f, 0.01f);
    EXPECT_EQ(result.check.samples, 220u);
    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.progress.samples, 220u);
    EXPECT_FALSE(result.haveSphere);
}

// The session sets the minimum a check needs, not the check itself.
TEST(MagCalSession, AShortCheckDoesNotPass)
{
    MagCalibrationSession session;

    ASSERT_TRUE(session.startCheck(identity()));
    tumble(session, 12);

    const MagCalibrationSession::Result result = session.finish(true);

    EXPECT_FALSE(result.passed);
}

// -----------------------------------------------------------------------------
// Progress
// -----------------------------------------------------------------------------

TEST(MagCalSession, ProgressFollowsWhatAFitWasFed)
{
    MagCalibrationSession session;

    session.startFit(0);
    tumble(session, 75);

    const MagCalibrator::Progress progress = session.getProgress();

    EXPECT_EQ(progress.samples, 75u);
    EXPECT_FLOAT_EQ(progress.spanX, 100.0f);
    EXPECT_FLOAT_EQ(progress.spanY, 100.0f);
    EXPECT_FLOAT_EQ(progress.spanZ, 100.0f);
}

TEST(MagCalSession, ACheckReportsSamplesAndNoSpans)
{
    MagCalibrationSession session;

    ASSERT_TRUE(session.startCheck(identity()));
    tumble(session, 40);

    const MagCalibrator::Progress progress = session.getProgress();

    EXPECT_EQ(progress.samples, 40u);
    EXPECT_FLOAT_EQ(progress.spanX, 0.0f);
    EXPECT_FLOAT_EQ(progress.spanY, 0.0f);
    EXPECT_FLOAT_EQ(progress.spanZ, 0.0f);
}

// A new run reads as empty before its first sample, not as the last run.
TEST(MagCalSession, StartingAgainForgetsWhatWasFed)
{
    MagCalibrationSession session;

    session.startFit(0);
    tumble(session, 75);
    (void) session.finish(true);

    session.startFit(0);

    const MagCalibrator::Progress progress = session.getProgress();
    EXPECT_EQ(progress.samples, 0u);
    EXPECT_FLOAT_EQ(progress.spanX, 0.0f);

    tumble(session, 520);
    EXPECT_EQ(session.finish(true).progress.samples, 520u);
}
