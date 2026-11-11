/**
 ******************************************************************************
 * @file    BodySubSensorHeartRateEx.cpp
 * @brief   SubSensor for HEART_RATE_EX (opt-in multi-source heart rate).
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Body.Sub.HRx"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Body/BodySubSensorHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "System.hpp"

using namespace Interface;

namespace Sensor
{

// The arbiter's HrSource is written onto the wire as the EX SOURCE field and
// read back by SDK::SensorDataParser::HeartRateEx::Source. Lock the two enums
// together so a value drift fails the build rather than mis-tagging provenance.
using ExSource = SDK::SensorDataParser::HeartRateEx::Source;
static_assert(static_cast<uint8_t>(HrSource::None) ==
        static_cast<uint8_t>(ExSource::UNKNOWN),
        "HrSource::None must match HeartRateEx::Source::UNKNOWN");
static_assert(static_cast<uint8_t>(HrSource::Optical) ==
        static_cast<uint8_t>(ExSource::OPTICAL),
        "HrSource::Optical must match HeartRateEx::Source::OPTICAL");
static_assert(static_cast<uint8_t>(HrSource::External) ==
        static_cast<uint8_t>(ExSource::EXTERNAL),
        "HrSource::External must match HeartRateEx::Source::EXTERNAL");

BodySubSensorHeartRateEx::BodySubSensorHeartRateEx(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::HEART_RATE_EX,
              SDK::SensorDataParser::HeartRateEx::getFieldsNumber(),
              ctrl)
    , mTick()
    , mDataMutex()
{
    mDriver.setSourceTick(&mTick);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& BodySubSensorHeartRateEx::getDriver()
{
    return mDriver;
}

bool BodySubSensorHeartRateEx::isActive()
{
    return mTick.isActive();
}

float BodySubSensorHeartRateEx::start(float period)
{
    LOG_INFO("start the HEART_RATE_EX subsensor\n");
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

void BodySubSensorHeartRateEx::stop()
{
    LOG_INFO("stop the HEART_RATE_EX subsensor\n");
    mTick.stop();
}

float BodySubSensorHeartRateEx::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float BodySubSensorHeartRateEx::getMinPeriod()
{
    return mMinPeriod;
}

float BodySubSensorHeartRateEx::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* BodySubSensorHeartRateEx::getDescription()
{
    return "Heart Rate (multi-source) sensor";
}

void BodySubSensorHeartRateEx::sensorRefresh()
{
    if (!mDriver.tickDue()) {
        return;
    }

    uint8_t  arbBpm, arbTrust, optBpm, optTrust, extBpm, extTrust;
    HrSource source;
    {
        OS::MutexCS locker(mDataMutex);
        arbBpm = mArbBpm; arbTrust = mArbTrust; source = mSource;
        optBpm = mOptBpm; optTrust = mOptTrust;
        extBpm = mExtBpm; extTrust = mExtTrust;
    }

    using Ex = SDK::SensorDataParser::HeartRateEx;
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(::Driver::System::GetTimeMs());
    sample.f[Ex::BPM]            = static_cast<float>(arbBpm);
    sample.f[Ex::TRUST_LEVEL]    = static_cast<float>(arbTrust);
    sample.f[Ex::SOURCE]         = static_cast<float>(source);
    sample.f[Ex::OPTICAL_BPM]    = static_cast<float>(optBpm);
    sample.f[Ex::OPTICAL_TRUST]  = static_cast<float>(optTrust);
    sample.f[Ex::EXTERNAL_BPM]   = static_cast<float>(extBpm);
    sample.f[Ex::EXTERNAL_TRUST] = static_cast<float>(extTrust);
    mDriver.pushDataSample();
}

void BodySubSensorHeartRateEx::setData(uint8_t arbBpm, uint8_t arbTrust, HrSource source,
                                       uint8_t optBpm, uint8_t optTrust,
                                       uint8_t extBpm, uint8_t extTrust)
{
    if (!isActive()) {
        return;
    }
    OS::MutexCS locker(mDataMutex);
    mArbBpm = arbBpm; mArbTrust = arbTrust; mSource = source;
    mOptBpm = optBpm; mOptTrust = optTrust;
    mExtBpm = extBpm; mExtTrust = extTrust;
}

} // namespace Sensor
