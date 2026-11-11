/**
 ******************************************************************************
 * @file    ImuSubSensorStepDetector.cpp
 * @brief   Step-detector sub-sensor for BMI270
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.StepDet"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorStepDetector.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp"

namespace Sensor
{

ImuSubSensorStepDetector::ImuSubSensorStepDetector(Interface::IImu&           imu,
                                                   Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::STEP_DETECTOR,
              SDK::SensorDataParser::StepDetector::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
    , mImu(imu)
{
    mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorStepDetector::getDriver()
{
    return mDriver;
}

bool ImuSubSensorStepDetector::isActive()
{
    return mActive.load();
}

float ImuSubSensorStepDetector::start(float period)
{
    LOG_INFO("start\n");
    bool expected = false;

    if (mActive.compare_exchange_strong(expected, true)) {
        if (!mImu.enableStepDetector()) {
            mActive.store(false);
            return 0.0f;
        }
        mQueue.clear();
    }

    return period;
}

void ImuSubSensorStepDetector::stop()
{
    LOG_INFO("stop\n");
    bool expected = true;

    if (mActive.compare_exchange_strong(expected, false)) {
        if (!mImu.disableStepDetector()) {
            LOG_ERROR("failed to disable step detector\n");
            mActive.store(true);
            return;
        }
        mQueue.clear();
    }
}

const char* ImuSubSensorStepDetector::getDescription()
{
    return "BMI270 step detector";
}

// Intentionally empty: a step-detected event is a momentary edge, not a
// persistent state. There is no current value to hand a freshly connected
// listener, and replaying the last event would deliver a stale step.
void ImuSubSensorStepDetector::newConnection()
{
}

void ImuSubSensorStepDetector::sensorRefresh()
{
    uint64_t timestamp;
    while (isActive() && mQueue.pop(timestamp, 0)) {
        publishData(timestamp);
    }
}

void ImuSubSensorStepDetector::handleStepDetected(uint64_t timestamp)
{
    if (!isActive()) {
        return;
    }

    LOG_DEBUG("rx step\n");
    mQueue.push(timestamp, 1);
    Sensor::Manager::getInstance().wake();
}

void ImuSubSensorStepDetector::publishData(uint64_t timestampUs)
{
    if (!isActive()) {
        return;
    }

    LOG_DEBUG("tx step\n");
    auto& sample = mDriver.getDataSample();
    sample.setTimestampUs(timestampUs);
    sample.u[SDK::SensorDataParser::StepDetector::Field::STEP_DETECTED] = 1;
    mDriver.pushDataSample();
}

} // namespace Sensor
