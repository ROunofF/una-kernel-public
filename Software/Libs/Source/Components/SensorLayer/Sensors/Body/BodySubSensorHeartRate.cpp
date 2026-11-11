/**
 ******************************************************************************
 * @file    BodySubSensorHeartRate.cpp
 * @brief   SubSensor for the Heart Rate
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Body.Sub.HR"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Body/BodySubSensorHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "System.hpp"

using namespace Interface;

namespace Sensor
{

BodySubSensorHeartRate::BodySubSensorHeartRate(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::HEART_RATE,
              // Stable 2-field layout (BPM + TRUST_LEVEL). HR provenance + the
              // raw per-source readings go out on the opt-in HEART_RATE_EX type.
              SDK::SensorDataParser::HeartRate::getFieldsNumber(),
              ctrl)
    , mTick()
    , mHr(0)
    , mTrustLevel(0)
    , mDataMutex()
{
    mDriver.setSourceTick(&mTick);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& BodySubSensorHeartRate::getDriver()
{
    return mDriver;
}

bool BodySubSensorHeartRate::isActive()
{
    return mTick.isActive();
}

float BodySubSensorHeartRate::start(float period)
{
    LOG_INFO("start the Heart Rate subsensor\n");
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());

    return period;
}

void BodySubSensorHeartRate::stop()
{
    LOG_INFO("stop the Heart Rate subsensor\n");
    mTick.stop();
}

float BodySubSensorHeartRate::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float BodySubSensorHeartRate::getMinPeriod()
{
    return mMinPeriod;
}

float BodySubSensorHeartRate::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* BodySubSensorHeartRate::getDescription()
{
    return "Heart Rate sensor";
}

void BodySubSensorHeartRate::sensorRefresh()
{
    if (!mDriver.tickDue()) {
        return;
    }

    uint8_t hr;
    uint8_t trustLevel;
    {
        OS::MutexCS locker(mDataMutex);
        // Values are already wrist-gated by SensorBody::publishHr (centralised
        // gating); just publish them.
        hr         = mHr;
        trustLevel = mTrustLevel;
    }

    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(::Driver::System::GetTimeMs());
    sample.f[SDK::SensorDataParser::HeartRate::Field::BPM]         = static_cast<float>(hr);
    sample.f[SDK::SensorDataParser::HeartRate::Field::TRUST_LEVEL] = static_cast<float>(trustLevel);
    mDriver.pushDataSample();
}

void BodySubSensorHeartRate::setData(uint8_t hr, uint8_t trustLevel)
{
    if (!isActive()) {
        return;
    }

    OS::MutexCS locker(mDataMutex);

    mHr         = hr;
    mTrustLevel = trustLevel;
}

} // namespace Sensor
