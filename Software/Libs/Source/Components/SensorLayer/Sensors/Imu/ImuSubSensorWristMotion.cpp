/**
 ******************************************************************************
 * @file    ImuSubSensorWristMotion.cpp
 * @brief   Wrist-motion sub-sensor for BMI270
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.Wrist"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorWristMotion.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

namespace Sensor
{

ImuSubSensorWristMotion::ImuSubSensorWristMotion(Interface::IImu&           imu,
                                                 Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::WRIST_MOTION,
              SDK::SensorDataParser::WristMotion::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
    , mImu(imu)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorWristMotion::getDriver()
{
    return mDriver;
}

bool ImuSubSensorWristMotion::isActive()
{
    return mActive;
}

float ImuSubSensorWristMotion::start(float period)
{
    LOG_INFO("start\n");

    mImu.enableWristWakeup();
    mActive = true;

    return period;
}

void ImuSubSensorWristMotion::stop()
{
    LOG_INFO("stop\n");

    mActive = false;
    mImu.disableWristWakeup();
}

const char* ImuSubSensorWristMotion::getDescription()
{
    return "BMI270 wrist motion";
}

// Intentionally empty: a wrist-motion event is a momentary edge, not a
// persistent state. There is no current value to hand a freshly connected
// listener, and replaying the last event would deliver a stale pulse.
void ImuSubSensorWristMotion::newConnection()
{
}

void ImuSubSensorWristMotion::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void ImuSubSensorWristMotion::handleWristMotion(uint64_t timestamp)
{
    if (!isActive()) {
        return;
    }

    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx wrist\n");
    mTimestamp = timestamp;
    mDirty     = true;
    Sensor::Manager::getInstance().wake();
}

void ImuSubSensorWristMotion::publishData()
{
    LOG_DEBUG("tx wrist\n");
    auto& sample = mDriver.getDataSample();
    sample.setTimestampUs(mTimestamp);
    sample.u[SDK::SensorDataParser::WristMotion::Field::WRIST_MOTION] = 1;
    mDriver.pushDataSample();
}

} // namespace Sensor
