/**
 ******************************************************************************
 * @file    ImuSubSensorActivityRecognition.cpp
 * @brief   Activity-recognition sub-sensor for BMI270
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.ActRec"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorActivityRecognition.hpp"
#include "SensorLayer/Core/SensorManager.hpp"

namespace Sensor
{

ImuSubSensorActivityRecognition::ImuSubSensorActivityRecognition(Interface::IImu&           imu,
                                                                 Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::ACTIVITY_RECOGNITION,
              SDK::SensorDataParser::ActivityRecognition::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
    , mImu(imu)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorActivityRecognition::getDriver()
{
    return mDriver;
}

bool ImuSubSensorActivityRecognition::isActive()
{
    return mActive;
}

float ImuSubSensorActivityRecognition::start(float period)
{
    LOG_INFO("start\n");

    mImu.enableActivity();
    mActive = true;

    return period;
}

void ImuSubSensorActivityRecognition::stop()
{
    LOG_INFO("stop\n");

    mActive = false;
    mImu.disableActivity();
}

const char* ImuSubSensorActivityRecognition::getDescription()
{
    return "BMI270 activity recognition";
}

void ImuSubSensorActivityRecognition::newConnection()
{
    OS::MutexCS cs(mDataMutex);
    mData.ts = OS::GetTimeMs() * 1000ULL;
    publishData();
}

void ImuSubSensorActivityRecognition::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void ImuSubSensorActivityRecognition::handleActivityDetected(
        SDK::SensorDataParser::ActivityRecognition::Activity id,
        uint64_t                                             timestamp)
{
    if (!isActive()) {
        return;
    }

    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx activity=%u\n", static_cast<uint32_t>(id));
    if (mData.activity != static_cast<uint8_t>(id)) {
        mData.ts         = timestamp;
        mData.activity   = static_cast<uint8_t>(id);
        mData.confidence = 1;
        mDirty           = true;
        Sensor::Manager::getInstance().wake();
    }
}

void ImuSubSensorActivityRecognition::publishData()
{
    LOG_DEBUG("tx activity=%u\n", static_cast<uint32_t>(mData.activity));
    auto& sample = mDriver.getDataSample();
    sample.setTimestampUs(mData.ts);
    sample.u[SDK::SensorDataParser::ActivityRecognition::Field::ID]         = mData.activity;
    sample.u[SDK::SensorDataParser::ActivityRecognition::Field::CONFIDENCE] = mData.confidence;
    mDriver.pushDataSample();
}

} // namespace Sensor
