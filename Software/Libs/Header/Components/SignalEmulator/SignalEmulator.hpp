#pragma once

#include "SwTimer.hpp"
#include "LinearInterpolator.hpp"

#include <cstdint>
#include <vector>

namespace Tools {

class SignalEmulator {
public:
    SignalEmulator(float noiseAmplitude = 0.0f);

    void begin(float value);
    void hold(uint32_t ms, float value);
    void moveTo(uint32_t ms, float value);
    void moveBy(uint32_t ms, float delta);

    void  setNoiseAmplitude(float amplitude);
    float getNoiseAmplitude() const;

    float getValue();

    uint32_t getLastTime()  const;
    float    getLastValue() const;

private:
    float smoothNoise();

private:
    Driver::SwTimer                 mTimer;
    std::vector<LinearInterpolator> mLines;

    uint32_t mLastTime;
    uint8_t  mLap;

    float    mNoiseAmplitude;
    uint32_t mNoiseState;
    float    mNoiseValue;
};

}
