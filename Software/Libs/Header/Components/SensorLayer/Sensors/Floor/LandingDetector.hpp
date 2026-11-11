/**
 ******************************************************************************
 * @file    LandingDetector.hpp
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

#ifndef __LANDING_DETECTOR_HPP
#define __LANDING_DETECTOR_HPP

#include <stdint.h>

#include "Filters/SimpleLPF.hpp"
#include "SwTimer.hpp"

class LandingDetector
{
public:
    struct Config
    {
        float    derivativeThreshold;
        uint16_t confirmInterval;
        float    derivativeLpfAlpha;
    };

    explicit LandingDetector(const Config& config);

    void reset();

    bool  process(float value, uint32_t ts);
    bool  isLanding()    const;
    float landingLevel() const;
    float getDerivate()  const;

private:
    const Config      mConfig;
    Filter::SimpleLPF mValue;
    Filter::SimpleLPF mDerivate;
    Driver::SwTimer   mTimer;
    bool              mLanding;
    uint32_t          mLastTS;
};

#endif
