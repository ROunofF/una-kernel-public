#define LOG_MODULE_PRX      "SignalEmulator"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SignalEmulator/SignalEmulator.hpp"

namespace Tools {

SignalEmulator::SignalEmulator(float noiseAmplitude)
    : mTimer(0xFFFFFFFF)
    , mLines()
    , mLastTime(0)
    , mLap(0)
    , mNoiseAmplitude(noiseAmplitude)
    , mNoiseState(0x12345678u)
    , mNoiseValue(0.0f)
{
}

void SignalEmulator::begin(float value)
{
    mLines.clear();
    mLines.emplace_back(0.0f, 0.0f, value, value);

    mLastTime = 0;
    mLap      = 0;
}

void SignalEmulator::hold(uint32_t ms, float value)
{
    mLines.emplace_back(getLastTime(),
                        getLastTime() + static_cast<float>(ms),
                        value,
                        value);
}

void SignalEmulator::moveBy(uint32_t ms, float delta)
{
    mLines.emplace_back(getLastTime(),
                        getLastTime() + static_cast<float>(ms),
                        getLastValue(),
                        getLastValue() + delta);
}

void SignalEmulator::moveTo(uint32_t ms, float value)
{
    mLines.emplace_back(getLastTime(),
                        getLastTime() + static_cast<float>(ms),
                        getLastValue(),
                        value);
}

void SignalEmulator::setNoiseAmplitude(float amplitude)
{
    mNoiseAmplitude = amplitude;
}

float SignalEmulator::getNoiseAmplitude() const
{
    return mNoiseAmplitude;
}

float SignalEmulator::smoothNoise()
{
    mNoiseState = mNoiseState * 1664525u + 1013904223u;

    const uint32_t v = (mNoiseState >> 24) & 0xFFu;

    const float target = static_cast<float>(v) / 255.0f - 0.5f; // -0.5 .. +0.5

    mNoiseValue = mNoiseValue * 0.9f + target * 0.1f;

    return mNoiseValue * mNoiseAmplitude;
}

float SignalEmulator::getValue()
{
    if (mLines.empty()) {
        return 0.0f;
    }

    const uint32_t totalTime = getLastTime();

    if (totalTime == 0U) {
        return getLastValue();
    }

    const uint32_t ts = mTimer.passed() % totalTime;

    if (mLastTime > ts) {
        ++mLap;
    }

    mLastTime = ts;

//    LOG_INFO_WP("%u:%u%%\n",
//                static_cast<uint32_t>(mLap + 1),
//                static_cast<uint32_t>(ts * 100 / totalTime));

    for (const auto& l : mLines) {
        if (ts <= static_cast<uint32_t>(l.getX2())) {
            return l.f(static_cast<float>(ts));
        }
    }

    return 0.0f;
}

float SignalEmulator::getLastValue() const
{
    if (mLines.empty()) {
        return 0.0f;
    }

    return mLines.back().getY2();
}

uint32_t SignalEmulator::getLastTime() const
{
    if (mLines.empty()) {
        return 0U;
    }

    return static_cast<uint32_t>(mLines.back().getX2());
}

}
