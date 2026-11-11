/**
 ******************************************************************************
 * @file    MagCalibration.cpp
 * @brief   Magnetometer calibration record and the policy that accepts one.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagCalibration.hpp"

#include <cmath>

MagCalibration::MagCalibration()
    : mRecord()
    , mValid(false)
{
}

MagCalibration::Verdict MagCalibration::evaluate(const Record& record,
        uint8_t coveredDirections, const Limits& limits)
{
    if (record.samples < limits.minSamples) {
        return Verdict::TOO_FEW_SAMPLES;
    }

    // Coverage before radius: a tumble too narrow to pass leaves the fitted
    // radius meaningless, and the field would be blamed for the rotation.
    if (coveredDirections < limits.minCoveredDirections) {
        return Verdict::COVERAGE_TOO_NARROW;
    }

    if (!(record.radius >= limits.minRadius) ||
        !(record.radius <= limits.maxRadius)) {
        return Verdict::RADIUS_OUT_OF_RANGE;
    }

    if (record.residual > (limits.maxResidualFactor * record.radius)) {
        return Verdict::RESIDUAL_TOO_HIGH;
    }

    // Last, so the verdicts above keep their meaning. A NaN clears every
    // bound by failing every comparison, and a fit judged good must still
    // be a record the correction takes.
    if (!isUsable(record)) {
        return Verdict::NOT_USABLE;
    }

    return Verdict::OK;
}

const char* MagCalibration::describe(Verdict verdict)
{
    switch (verdict) {
        case Verdict::OK:
            return "accepted";

        case Verdict::TOO_FEW_SAMPLES:
            return "too few samples";

        case Verdict::COVERAGE_TOO_NARROW:
            return "rotation covered too little of the sphere";

        case Verdict::RESIDUAL_TOO_HIGH:
            return "samples do not lie on a sphere";

        case Verdict::RADIUS_OUT_OF_RANGE:
            return "implausible fitted radius";

        case Verdict::NOT_USABLE:
            return "not a usable correction";

        default:
            break;
    }

    return "unknown";
}

bool MagCalibration::set(const Record& record)
{
    if (!isUsable(record)) {
        return false;
    }

    mRecord = record;
    mValid  = true;

    return true;
}

void MagCalibration::clear()
{
    mRecord = Record();
    mValid  = false;
}

bool MagCalibration::isValid() const
{
    return mValid;
}

const MagCalibration::Record& MagCalibration::get() const
{
    return mRecord;
}

bool MagCalibration::apply(float& x, float& y, float& z) const
{
    if (!mValid) {
        return false;
    }

    // Offset first: the matrix describes the ellipsoid about its own centre.
    const float vx = x - mRecord.offsetX;
    const float vy = y - mRecord.offsetY;
    const float vz = z - mRecord.offsetZ;

    x = mRecord.m00 * vx + mRecord.m01 * vy + mRecord.m02 * vz;
    y = mRecord.m01 * vx + mRecord.m11 * vy + mRecord.m12 * vz;
    z = mRecord.m02 * vx + mRecord.m12 * vy + mRecord.m22 * vz;

    return true;
}

bool MagCalibration::isUsable(const Record& record)
{
    const float values[] = {
        record.offsetX, record.offsetY, record.offsetZ,
        record.m00, record.m01, record.m02,
        record.m11, record.m12, record.m22,
        record.radius, record.residual,
    };

    for (float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }

    // Tolerances are scaled by the radius; zero would collapse them all.
    if (!(record.radius > 0.0f)) {
        return false;
    }

    // An RMS distance cannot be negative.
    if (record.residual < 0.0f) {
        return false;
    }

    // Sylvester's criterion: a symmetric matrix is positive definite exactly
    // when its leading principal minors are all positive.
    const double m00 = record.m00;
    const double m01 = record.m01;
    const double m02 = record.m02;
    const double m11 = record.m11;
    const double m12 = record.m12;
    const double m22 = record.m22;

    if (!(m00 > 0.0)) {
        return false;
    }

    const double minor2 = m00 * m11 - m01 * m01;
    if (!(minor2 > 0.0)) {
        return false;
    }

    const double det = m00 * (m11 * m22 - m12 * m12)
                     - m01 * (m01 * m22 - m12 * m02)
                     + m02 * (m01 * m12 - m11 * m02);

    return det > 0.0;
}
