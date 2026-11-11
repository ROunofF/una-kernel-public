/**
 ******************************************************************************
 * @file    GradeEstimator.hpp
 * @brief   Barometer-only terrain-grade estimator over a rolling window.
 *
 * Computes terrain grade from a short relative barometric elevation change
 * divided by GPS-integrated horizontal distance over the same window. QNH /
 * sea-level calibration is never required because only a relative delta is
 * used.
 *
 * Pure logic (no hardware dependency) so it can be host unit-tested; the
 * SensorGrade kernel sensor feeds it barometer altitude and cumulative GPS
 * distance at 1 Hz and publishes its output.
 ******************************************************************************
 */

#ifndef __GRADE_ESTIMATOR_HPP
#define __GRADE_ESTIMATOR_HPP

#include <cstddef>
#include <cstdint>

#include "SensorLayer/Sensors/Grade/GradeEstimatorConfig.hpp"

namespace Sensor
{

class GradeEstimator {
public:
    struct Sample {
        float gradePct   = 0.0f;   ///< Terrain grade, % (positive = uphill).
        bool  gradeValid = false;  ///< Reliable current estimate.
    };

    GradeEstimator() { reset(); }

    /// Clear the window and outputs (call at session start).
    void reset();

    /**
     * @brief Feed one 1 Hz tick.
     * @param baroAltitudeM   Barometric (relative) altitude, metres.
     * @param cumGpsDistanceM Cumulative GPS horizontal distance, metres.
     * @param gpsSpeedValid   Whether GPS speed/distance is currently reliable.
     */
    void update(float baroAltitudeM, float cumGpsDistanceM, bool gpsSpeedValid);

    Sample getSample() const { return {mGradePct, mGradeValid}; }

private:
    struct Point {
        float alt;
        float dist;
    };

    Point    mBuf[GradeConfig::kWindowSamples] {};
    size_t   mCount     = 0;  ///< Total samples seen (saturating for index use).
    uint32_t mValidRun  = 0;  ///< Consecutive ticks with gpsSpeedValid == true.
    float    mGradePct  = 0.0f;
    bool     mGradeValid = false;
    float    mAltFiltM    = 0.0f;   ///< EMA-filtered altitude fed into the window.
    bool     mAltFiltInit = false;  ///< Filter seeded with the first sample.
};

} // namespace Sensor

#endif // __GRADE_ESTIMATOR_HPP
