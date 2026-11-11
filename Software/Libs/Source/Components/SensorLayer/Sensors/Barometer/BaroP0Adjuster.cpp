/**
 ******************************************************************************
 * @file    BaroP0Adjuster.cpp
 * @brief   Sea level pressure (P0) reference calculator.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Baro.P0.Adj"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Barometer/BaroP0Adjuster.hpp"

#include <cmath>
#include <limits>

namespace Sensor
{

bool BaroP0Adjuster::start(float altitude, uint32_t nowMs)
{
    // Written so that a NaN fails it.
    if (!((altitude >= skMinAltitudeM) && (altitude <= skMaxAltitudeM))) {
        LOG_WARNING("Altitude %.1f m is out of range, not started\n", altitude);
        return false;
    }

    mAltitude      = altitude;
    mPressureSumma = 0.0f;
    mCount         = 0;
    mStartedMs     = nowMs;
    mActive        = true;

    return true;
}

bool BaroP0Adjuster::expire(uint32_t nowMs)
{
    if (!mActive) {
        return false;
    }

    // Signed: a sample can carry an instant from just before the start, and
    // that is not a run long overdue.
    if (static_cast<int32_t>(nowMs - mStartedMs)
            <= static_cast<int32_t>(skDeadlineMs)) {
        return false;
    }

    mActive = false;

    LOG_WARNING("Abandoned with %u of %u samples\n",
            static_cast<unsigned>(mCount), static_cast<unsigned>(skSamples));

    return true;
}

bool BaroP0Adjuster::update(float pressure, uint32_t nowMs)
{
    if (!mActive || expire(nowMs)) {
        return false;
    }

    if ((mCount > 0) && ((nowMs - mLastSampleMs) > skMaxGapMs)) {
        mPressureSumma = 0.0f;
        mCount         = 0;
    }

    mLastSampleMs   = nowMs;
    mPressureSumma += pressure;

    if (++mCount < skSamples) {
        return false;
    }

    // Complete whatever comes out below.
    mActive = false;

    const float p0 = computeCurrentP0();
    if (!std::isfinite(p0)) {
        return false;
    }

    LOG_INFO("New altitude %.3f, P0 %.3f\n", mAltitude, p0);

    return true;
}

float BaroP0Adjuster::getP0() const
{
    const float p0 = computeCurrentP0();

    return std::isfinite(p0) ? p0 : skDefaultP0;
}

float BaroP0Adjuster::computeCurrentP0() const
{
    if (mCount == 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // mbar to Pa.
    const float averagePressure =
            (mPressureSumma / static_cast<float>(mCount)) * 100.0f;

    return computeP0FromAltitude(averagePressure, mAltitude);
}

float BaroP0Adjuster::computeP0FromAltitude(float p, float altitude)
{
    if (!(p > 0.0f)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // The formula holds while (1 - h / 44330) is positive, far above any
    // altitude a run is started for.
    const float x = 1.0f - (altitude / 44330.0f);
    if (!(x > 0.0f)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    static constexpr float n = 5.255877f;

    return p / std::pow(x, n);
}

} // namespace Sensor
