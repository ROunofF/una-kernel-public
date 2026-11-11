/**
 ******************************************************************************
 * @file    SensorImu.cpp
 * @brief   Sensor IMU
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "SensorIMU"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/SensorImu.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "System.hpp"
#include "OS.hpp"

#include <inttypes.h>

namespace Sensor
{

Imu::Imu(Interface::IImu& imu)
    : mImu(imu)
    , mAccelRaw(mImu, *this)
    , mAccel(mAccelRaw.getDriver(), *this)
    , mGyroRaw(mImu, *this)
    , mGyro(mGyroRaw.getDriver(), *this)
    , mFusionRaw(*this)
    , mFusion(mFusionRaw.getDriver(), *this)
    , mStepCounter(mImu, *this)
    , mStepDetector(mImu, *this)
    , mMotionDetector(mImu, *this)
    , mActivity(mImu, *this)
    , mWristMotion(mImu, *this)
    , mSubSensors{{&mAccel,
                   &mAccelRaw,
                   &mGyro,
                   &mGyroRaw,
                   &mFusion,
                   &mFusionRaw,
                   &mStepCounter,
                   &mStepDetector,
                   &mMotionDetector,
                   &mActivity,
                   &mWristMotion}}
{
    mImu.setSensorCallback(this);
}

Interface::IImu& Imu::getImu()
{
    return mImu;
}

Sensor::Driver& Imu::getDriverAccel()
{
    return mAccel.getDriver();
}

Sensor::Driver& Imu::getDriverAccelRaw()
{
    return mAccelRaw.getDriver();
}

Sensor::Driver& Imu::getDriverGyro()
{
    return mGyro.getDriver();
}

Sensor::Driver& Imu::getDriverGyroRaw()
{
    return mGyroRaw.getDriver();
}

Sensor::Driver& Imu::getDriverFusion()
{
    return mFusion.getDriver();
}

Sensor::Driver& Imu::getDriverFusionRaw()
{
    return mFusionRaw.getDriver();
}

Sensor::Driver& Imu::getDriverStepCounter()
{
    return mStepCounter.getDriver();
}

Sensor::Driver& Imu::getDriverStepDetector()
{
    return mStepDetector.getDriver();
}

Sensor::Driver& Imu::getDriverMotionDetect()
{
    return mMotionDetector.getDriver();
}

Sensor::Driver& Imu::getDriverActivityRecognition()
{
    return mActivity.getDriver();
}

Sensor::Driver& Imu::getDriverWristMotion()
{
    return mWristMotion.getDriver();
}

float Imu::sdcStart(Sensor::Driver* driver, float period)
{
    float p = getSubSensor(driver)->start(period);

    return p;
}

void Imu::sdcStop(Sensor::Driver* driver)
{
    getSubSensor(driver)->stop();
}

float Imu::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->updatePeriod(period);
}

void Imu::sdcUpdateLatency(Sensor::Driver* driver, uint32_t latency)
{
    getSubSensor(driver)->updateLatency(latency);
}

float Imu::sdcGetMinPeriod(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getMinPeriod();
}

const char* Imu::sdcGetDescription(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getDescription();
}

void Imu::sdcNewConnection(Sensor::Driver* driver)
{
    return getSubSensor(driver)->newConnection();
}

void Imu::onAccelRaw(const Interface::IImu::SensorRawData* data, size_t count, float scale, uint32_t periodUs)
{
    (void) scale;

    mAccelRaw.addNewData(data, count, periodUs);
}

void Imu::onGyroRaw(const Interface::IImu::SensorRawData* data,
                    size_t                                count,
                    float                                 scale,
                    uint32_t                              periodUs)
{
    (void) scale;

    mGyroRaw.addNewData(data, count, periodUs);
}

void Imu::onSyncRaw(const Interface::IImu::SyncSampleData* data,
                    size_t                                 count,
                    float                                  accelScale,
                    float                                  gyroScale,
                    uint32_t                               periodUs)
{
    (void) accelScale;
    (void) gyroScale;

    mFusionRaw.addNewData(data, count, periodUs);
}

void Imu::onActivity(Interface::IImu::ActivityType type, uint64_t timestamp)
{
    using Activity = SDK::SensorDataParser::ActivityRecognition::Activity;

    Activity activity;

    switch (type) {
        case Interface::IImu::ActivityType::STILL:   activity = Activity::STILL;   break;
        case Interface::IImu::ActivityType::WALKING: activity = Activity::WALKING; break;
        case Interface::IImu::ActivityType::RUNNING: activity = Activity::RUNNING; break;
        default:                              activity = Activity::UNKNOWN; break;
    }

    LOG_DEBUG("activity type %u\n", type);
    mActivity.handleActivityDetected(activity, timestamp);
}

void Imu::onNoMotion(uint64_t timestamp)
{
    (void) timestamp;

    LOG_DEBUG("no motion\n");
    mMotionDetector.motionDetected(Sensor::ImuSubSensorMotionDetect::Motion::NO);
}

void Imu::onMotion(uint64_t timestamp)
{
    (void) timestamp;

    LOG_DEBUG("any motion\n");
    mMotionDetector.motionDetected(Sensor::ImuSubSensorMotionDetect::Motion::ANY);
}

void Imu::onSigMotion(uint64_t timestamp)
{
    (void) timestamp;

    LOG_DEBUG("sig motion\n");
    mMotionDetector.motionDetected(Sensor::ImuSubSensorMotionDetect::Motion::SIG);
}

void Imu::onStepCounter(uint32_t steps, uint64_t timestamp)
{
    (void) timestamp;

    LOG_DEBUG("steps %u\n", steps);

    mStepCounter.updateStepCounter(steps);
}

void Imu::onStepDetector(uint64_t timestamp)
{
    LOG_DEBUG("step\n");
    mStepDetector.handleStepDetected(timestamp);
}

void Imu::onWristWakeup(uint64_t timestamp)
{
    LOG_DEBUG("wrist motion\n");
    mWristMotion.handleWristMotion(timestamp);
}

uint8_t Imu::getActiveCount()
{
    uint8_t summa = 0;

    for (auto s : mSubSensors) {
        if (s->isActive()) {
            ++summa;
        }
    }

    return summa;
}

Sensor::ISubSensor* Imu::getSubSensor(Sensor::Driver* driver)
{
    for (auto s : mSubSensors) {
        if (&s->getDriver() == driver) {
            return s;
        }
    }

    assert(false);

    return nullptr;
}

} // namespace Sensor
