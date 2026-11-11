/**
 ******************************************************************************
 * @file    CadenceEstimatorConfig.hpp
 * @brief   Tunables for windowed step-counter cadence estimation.
 ******************************************************************************
 */

#ifndef CADENCE_ESTIMATOR_CONFIG_HPP
#define CADENCE_ESTIMATOR_CONFIG_HPP

#include <cstdint>

namespace Sensor::CadenceConfig
{

/** Output sanity clamp only - the windowed rate is clamped into this range. */
constexpr float    kMinCadenceSpm       = 80.0f;
constexpr float    kMaxCadenceSpm       = 220.0f;

/** Step-counter samples older than this are evicted from the rate window. */
constexpr uint64_t kWindowUs            = 8'000'000;

/** Minimum retained-sample span before cadence counts as acquired. */
constexpr uint64_t kMinAcqWindowUs      = 3'000'000;

/** No step-counter sample for longer than this -> cadence invalid (stale). */
constexpr uint64_t kMaxStepGapUs        = 2'000'000;

/** Capacity of the retained step-counter sample ring. */
constexpr uint32_t kMaxSamples          = 48u;

// NOTE: step-length derivation (and its 0.15-2.50 m emit gate) now lives
// SDK-side in SDK::Calibration::StrideMath; the kernel emits cadence only.

static_assert(kMinCadenceSpm > 0.0f, "Minimum cadence must be positive");
static_assert(kMaxCadenceSpm > 0.0f, "Maximum cadence must be positive");
static_assert(kMinCadenceSpm < kMaxCadenceSpm, "Cadence range is invalid");
static_assert(kMinAcqWindowUs > 0u, "Acquisition window must be positive");
static_assert(kMinAcqWindowUs <= kWindowUs,
              "Acquisition window cannot exceed the rate window");
static_assert(kMaxStepGapUs > 0u, "Maximum step gap must be positive");
static_assert(kMaxStepGapUs <= kWindowUs,
              "Step gap bound must fit within the rate window");
static_assert(kMaxSamples >= 2u,
              "At least two retained samples are needed for a rate");

} // namespace Sensor::CadenceConfig

#endif // CADENCE_ESTIMATOR_CONFIG_HPP
