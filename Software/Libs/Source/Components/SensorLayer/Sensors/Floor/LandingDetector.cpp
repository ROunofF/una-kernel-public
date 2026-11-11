/**
 ******************************************************************************
 * @file    LandingDetector.cpp
 * @brief   Implementation of landing detection based on filtered signal
 *          derivative.
 *
 * This module detects when the pressure signal becomes stable enough to treat
 * the current state as a landing between floor transitions. Detection is based
 * on the absolute value of the filtered derivative remaining below a configured
 * threshold for a specified confirmation interval.
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "LandingDetector"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Floor/LandingDetector.hpp"

#include <cmath>

/**
 * @brief Constructs a landing detector with the specified configuration.
 *
 * Initializes internal filters, timer, and state flags.
 *
 * @param config Detector configuration parameters.
 */
LandingDetector::LandingDetector(const Config& config)
    : mConfig(config)
    , mValue(0.8f)
    , mDerivate(mConfig.derivativeLpfAlpha)
    , mTimer()
    , mLanding(false)
    , mLastTS(0)
{
    reset();
}

/**
 * @brief Resets detector state.
 *
 * Clears internal filtered values, restarts the confirmation timer, clears the
 * landing flag, and resets the last timestamp.
 */
void LandingDetector::reset()
{
    mValue.reset();
    mDerivate.reset();
    mTimer.start(mConfig.confirmInterval);
    mLanding = false;

    mLastTS    = 0;
}

/**
 * @brief Processes a new input sample.
 *
 * The method updates the filtered value, estimates the derivative using the
 * elapsed time between samples, and checks whether the derivative magnitude is
 * below the configured landing threshold.
 *
 * A landing is confirmed only if the derivative remains below the threshold for
 * the configured confirmation interval.
 *
 * @param value Input signal value.
 * @param ts    Timestamp of the sample in milliseconds.
 *
 * @retval true  Landing has just been detected at this call.
 * @retval false Landing has not been newly detected.
 *
 * @note Once landing is detected, subsequent calls return false until the
 *       detector leaves landing state and re-enters it again.
 */
bool LandingDetector::process(float value, uint32_t ts)
{
    if (mValue.isReseted()) {
        mValue.forceValue(value);
        mLastTS = ts;

        return false;
    }

    const uint32_t dtMs = Driver::SwTimer::passed(ts, mLastTS);

    if (dtMs == 0U) {
        return false;
    }

    const float lastValue     = mValue.getValue();
    const float dv            = mValue.execute(value) - lastValue;
    const float dt            = static_cast<float>(dtMs) / 1000.0f; // Convert to seconds
    const float derivative    = mDerivate.execute(dv / dt);
    const bool  landing       = (std::fabs(derivative) < mConfig.derivativeThreshold);

    mLastTS = ts;

    if (!landing) {
        mLanding = false;
        mTimer.reset();
        return false;
    }

    if (!mTimer.expired()) {
        return false;
    }

    if (mLanding) {
        return false;
    }

    LOG_DEBUG("landing begin\n");

    mLanding = true;

    return true;
}

/**
 * @brief Returns whether the detector is currently in landing state.
 *
 * @retval true  The signal is currently considered stable enough to be a landing.
 * @retval false The detector is not in landing state.
 */
bool LandingDetector::isLanding() const
{
    return mLanding;
}

/**
 * @brief Returns the current filtered landing level.
 *
 * This value corresponds to the filtered signal level associated with the
 * detected landing.
 *
 * @return Current filtered value.
 */
float LandingDetector::landingLevel() const
{
    return mValue.getValue();
}

/**
 * @brief Returns the current filtered derivative value.
 *
 * @return Current derivative estimate.
 */
float LandingDetector::getDerivate() const
{
    return mDerivate.getValue();
}
