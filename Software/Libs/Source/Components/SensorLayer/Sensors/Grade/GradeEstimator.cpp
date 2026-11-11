/**
 ******************************************************************************
 * @file    GradeEstimator.cpp
 * @brief   Implementation of the barometric terrain-grade estimator.
 ******************************************************************************
 */

#include "SensorLayer/Sensors/Grade/GradeEstimator.hpp"

#include <cmath>

namespace Sensor
{

void GradeEstimator::reset()
{
    mCount       = 0;
    mValidRun    = 0;
    mGradePct    = 0.0f;
    mGradeValid  = false;
    mAltFiltM    = 0.0f;
    mAltFiltInit = false;
}

void GradeEstimator::update(float baroAltitudeM, float cumGpsDistanceM,
                            bool gpsSpeedValid)
{
    // Reject non-finite inputs outright: NaN/Inf must never enter the window
    // (they would poison future reference samples and bypass the denom check).
    if (!std::isfinite(baroAltitudeM) || !std::isfinite(cumGpsDistanceM)) {
        mValidRun   = 0;
        mGradeValid = false;
        return;
    }

    // GPS validity must hold for the whole window; any gap restarts the count
    // of consecutive reliable seconds.
    if (gpsSpeedValid) {
        ++mValidRun;
    } else {
        mValidRun = 0;
    }

    // Low-pass the altitude before it enters the window. The raw 2-point baro
    // delta is dominated by sensor noise on flat ground; an EMA rejects
    // it while preserving a sustained slope once settled (both window endpoints
    // lag equally), so real hills are still caught. Seed on the first sample to
    // avoid a spurious startup ramp.
    if (!mAltFiltInit) {
        mAltFiltM    = baroAltitudeM;
        mAltFiltInit = true;
    } else {
        mAltFiltM += GradeConfig::kAltFilterAlpha * (baroAltitudeM - mAltFiltM);
    }

    const size_t idx = mCount % GradeConfig::kWindowSamples;
    mBuf[idx] = {mAltFiltM, cumGpsDistanceM};
    ++mCount;

    mGradeValid = false;

    // Need a full window of samples, all with valid GPS, before estimating.
    if (mCount < GradeConfig::kWindowSamples ||
        mValidRun < GradeConfig::kWindowSamples) {
        return;
    }

    // The sample one window behind the current one is the "10 s ago" reference.
    const size_t refIdx = (idx + 1) % GradeConfig::kWindowSamples;
    const float  denom  = cumGpsDistanceM - mBuf[refIdx].dist;

    // Require a minimum horizontal travel over the window before dividing: a
    // near-zero positive denom (standing still / GPS jitter) would amplify the
    // baro delta into a huge but finite — and meaningless — grade.
    if (denom <= GradeConfig::kMinWindowDistanceM) {
        return;
    }

    // Both endpoints use the EMA-filtered altitude (mAltFiltM == mBuf[idx].alt)
    // so the noise rejection applies to the current end too; mixing raw-now with
    // filtered-old would re-inject a constant lag offset into the grade.
    mGradePct   = (mAltFiltM - mBuf[refIdx].alt) / denom * 100.0f;
    mGradeValid = std::isfinite(mGradePct);
}

} // namespace Sensor
