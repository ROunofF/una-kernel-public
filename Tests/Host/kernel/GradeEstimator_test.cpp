#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "SensorLayer/Sensors/Grade/GradeEstimator.hpp"
#include "SensorLayer/Sensors/Grade/GradeEstimatorConfig.hpp"

using Sensor::GradeEstimator;
namespace GC = Sensor::GradeConfig;

TEST(GradeEstimator, InvalidBeforeWindowFills)
{
    GradeEstimator g;
    // One short of a full window → still invalid.
    for (size_t i = 0; i < GC::kWindowSamples - 1; ++i) {
        g.update(static_cast<float>(i) * 0.05f, static_cast<float>(i) * 10.0f, true);
    }
    EXPECT_FALSE(g.getSample().gradeValid);
}

TEST(GradeEstimator, ComputesConstantUphillGrade)
{
    GradeEstimator g;
    // 0.05 m rise per 10 m over the 10 s window → 0.5 m / 100 m = 0.5%.
    // Run well past the altitude EMA's settling time: on a constant slope the
    // filter converges to the true line minus a constant lag offset, so the
    // window endpoint difference equals the true rise exactly.
    float alt = 0.0f;
    float dist = 0.0f;
    for (size_t i = 0; i < GC::kWindowSamples + 70; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    const auto s = g.getSample();
    ASSERT_TRUE(s.gradeValid);
    EXPECT_NEAR(s.gradePct, 0.5f, 1e-3f);
}

TEST(GradeEstimator, ComputesNegativeDownhillGrade)
{
    GradeEstimator g;
    float alt = 100.0f;
    float dist = 0.0f;
    // Past the EMA settling time (see ComputesConstantUphillGrade) so the
    // constant descending slope is reported exactly.
    for (size_t i = 0; i < GC::kWindowSamples + 70; ++i) {
        alt -= 0.2f;     // descending 2 m over the window
        dist += 10.0f;   // 100 m
        g.update(alt, dist, true);
    }
    const auto s = g.getSample();
    ASSERT_TRUE(s.gradeValid);
    EXPECT_NEAR(s.gradePct, -2.0f, 1e-3f);  // -2 m / 100 m
}

TEST(GradeEstimator, LowPassRejectsAltitudeNoiseOnFlat)
{
    // Regression for the on-device finding: flat ground (constant true altitude)
    // with heavy broadband baro noise. The raw 2-point grade swung ±15% and
    // tripped the ±3% acceptance gate; the altitude EMA must keep the reported
    // grade small so flat segments qualify.
    GradeEstimator g;
    float dist = 0.0f;
    uint32_t rng = 12345u;  // deterministic LCG → reproducible "noise"
    float sumAbs = 0.0f, maxAbs = 0.0f;
    size_t n = 0;
    for (size_t i = 0; i < GC::kWindowSamples + 200; ++i) {
        dist += 10.0f;  // 100 m window → 1 m of rise ≈ 1% grade
        rng = rng * 1103515245u + 12345u;
        const float noise =
            (static_cast<float>((rng >> 16) % 1000) / 1000.0f - 0.5f) * 5.0f; // ±2.5 m
        g.update(50.0f + noise, dist, true);
        if (g.getSample().gradeValid) {
            const float a = std::fabs(g.getSample().gradePct);
            sumAbs += a;
            if (a > maxAbs) { maxAbs = a; }
            ++n;
        }
    }
    ASSERT_GT(n, 0u);
    // Raw 2-point grade on this profile swings several percent; the EMA must
    // keep the median behaviour small and never breach the ±3% acceptance gate.
    EXPECT_LT(sumAbs / static_cast<float>(n), 1.0f);  // mean well under the gate
    EXPECT_LT(maxAbs, 3.0f);                          // never trips GRADE_MAX_PCT
}

TEST(GradeEstimator, InvalidWhenStandingStill)
{
    GradeEstimator g;
    // Distance does not advance → zero denominator → invalid (no div-by-zero).
    for (size_t i = 0; i < GC::kWindowSamples + 2; ++i) {
        g.update(50.0f, 500.0f, true);
    }
    EXPECT_FALSE(g.getSample().gradeValid);
}

TEST(GradeEstimator, InvalidWhenWindowDistanceBelowFloor)
{
    GradeEstimator g;
    // Tiny positive distance per tick → total window run stays under the
    // kMinWindowDistanceM floor → grade must NOT be computed (jitter guard),
    // even though the altitude rises (which would otherwise amplify hugely).
    float dist = 0.0f;
    const float perTick =
        (GC::kMinWindowDistanceM / static_cast<float>(GC::kWindowSamples)) * 0.5f;
    for (size_t i = 0; i < GC::kWindowSamples + 2; ++i) {
        dist += perTick;
        g.update(50.0f + static_cast<float>(i) * 0.5f, dist, true);
    }
    EXPECT_FALSE(g.getSample().gradeValid);
}

TEST(GradeEstimator, InvalidAfterGpsLoss)
{
    GradeEstimator g;
    float alt = 0.0f;
    float dist = 0.0f;
    for (size_t i = 0; i < GC::kWindowSamples; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    ASSERT_TRUE(g.getSample().gradeValid);

    // A single invalid-GPS tick invalidates the estimate immediately and for
    // the next full window.
    alt += 0.05f;
    dist += 10.0f;
    g.update(alt, dist, false);
    EXPECT_FALSE(g.getSample().gradeValid);

    // Recovery requires another full window of valid ticks.
    for (size_t i = 0; i < GC::kWindowSamples - 1; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    EXPECT_FALSE(g.getSample().gradeValid);  // one short

    alt += 0.05f;
    dist += 10.0f;
    g.update(alt, dist, true);
    EXPECT_TRUE(g.getSample().gradeValid);
}

TEST(GradeEstimator, InvalidOnNonFiniteInputs)
{
    GradeEstimator g;
    float alt = 0.0f;
    float dist = 0.0f;
    for (size_t i = 0; i < GC::kWindowSamples; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    ASSERT_TRUE(g.getSample().gradeValid);

    // A NaN altitude must invalidate the estimate and not poison the window.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    g.update(nan, dist + 10.0f, true);
    EXPECT_FALSE(g.getSample().gradeValid);

    // Recovery requires another full window of finite, valid samples.
    for (size_t i = 0; i < GC::kWindowSamples; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    EXPECT_TRUE(g.getSample().gradeValid);
}

TEST(GradeEstimator, ResetClearsState)
{
    GradeEstimator g;
    float alt = 0.0f;
    float dist = 0.0f;
    for (size_t i = 0; i < GC::kWindowSamples; ++i) {
        alt += 0.05f;
        dist += 10.0f;
        g.update(alt, dist, true);
    }
    ASSERT_TRUE(g.getSample().gradeValid);

    g.reset();
    EXPECT_FALSE(g.getSample().gradeValid);
    // Must refill the window before becoming valid again.
    g.update(0.0f, 10.0f, true);
    EXPECT_FALSE(g.getSample().gradeValid);
}
