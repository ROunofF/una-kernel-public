/**
 ******************************************************************************
 * @file    GradeEstimatorConfig.hpp
 * @brief   Tunables for the barometric terrain-grade estimator.
 ******************************************************************************
 */

#ifndef __GRADE_ESTIMATOR_CONFIG_HPP
#define __GRADE_ESTIMATOR_CONFIG_HPP

#include <cstddef>

namespace Sensor
{

namespace GradeConfig
{

/// Rolling window for barometric grade computation, seconds.
constexpr float kGradeWindowS = 10.0f;

/// Minimum average speed over the window for a meaningful grade, m/s. Below
/// this the GPS-distance denominator is dominated by position noise rather than
/// real travel, so any grade derived from it is meaningless. Mirrors the
/// calibrator's GPS-speed acceptance floor (GPS_SPEED_MIN) so the grade sensor
/// only reports a value when the user is genuinely moving.
constexpr float kMinWindowSpeedMps = 0.5f;

/// Minimum horizontal distance over the window (metres) required before a grade
/// is computed — the distance implied by kMinWindowSpeedMps sustained across the
/// whole window (0.5 m/s * 10 s = 5 m). Below this the barometric delta divided
/// by a near-zero, noise-dominated distance would amplify into a huge but
/// meaningless grade, so the estimate is skipped.
constexpr float kMinWindowDistanceM = kMinWindowSpeedMps * kGradeWindowS;

/// EMA low-pass coefficient applied to the barometric altitude BEFORE the
/// 2-point grade difference. The raw per-sample baro signal is far too
/// noisy on flat ground (observed ±15 % grade swings vs a true ~0 %), which the
/// GRADE_MAX_PCT gate then rejects. Smaller alpha = smoother / more lag; at 1 Hz
/// alpha = 0.15 is roughly a 6 s time constant. A constant slope is preserved
/// once the filter settles (both window endpoints lag equally), so real hills
/// are still detected — only noise is attenuated. Tune from on-device traces.
constexpr float kAltFilterAlpha = 0.15f;

/// Ring-buffer length for 1 Hz sampling. Spanning a kGradeWindowS-second
/// window with BOTH endpoints included (t = 0 and t = -kGradeWindowS) needs
/// kGradeWindowS + 1 samples — e.g. 10 s -> 11 entries (now plus the ten
/// prior seconds). GradeEstimator reads the "window-ago" reference as the
/// slot kGradeWindowS positions back, i.e. (idx + 1) % kWindowSamples.
constexpr size_t kWindowSamples = static_cast<size_t>(kGradeWindowS) + 1;

} // namespace GradeConfig

} // namespace Sensor

#endif // __GRADE_ESTIMATOR_CONFIG_HPP
