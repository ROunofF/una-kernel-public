/**
 ******************************************************************************
 * @file    ImuSubSensorStepCounter.cpp
 * @brief   Step-counter sub-sensor for BMI270
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.StepCnt"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorStepCounter.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"

namespace Sensor
{

ImuSubSensorStepCounter::ImuSubSensorStepCounter(Interface::IImu&           imu,
                                                 Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::STEP_COUNTER,
              SDK::SensorDataParser::StepCounter::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
    , mImu(imu)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorStepCounter::getDriver()
{
    return mDriver;
}

bool ImuSubSensorStepCounter::isActive()
{
    return mActive;
}

float ImuSubSensorStepCounter::start(float period)
{
    LOG_INFO("start\n");

    mImu.enableStepCounter();
    mActive = true;

    return period;
}

void ImuSubSensorStepCounter::stop()
{
    LOG_INFO("stop\n");

    mActive = false;
    mImu.disableStepCounter();
}

const char* ImuSubSensorStepCounter::getDescription()
{
    return "BMI270 step counter";
}

void ImuSubSensorStepCounter::newConnection()
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void ImuSubSensorStepCounter::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void ImuSubSensorStepCounter::updateStepCounter(uint32_t raw)
{
    if (!isActive()) {
        return;
    }

    OS::MutexCS cs(mDataMutex);

    LOG_DEBUG("rx raw=%u\n", raw);

    const uint32_t before = getData();

    if (!mPrimed) {
        // First reading after construction - adopt it as the epoch baseline
        // so the exposed total starts at 0 regardless of any value the chip
        // retained across an MCU reboot.
        mEpochBase = raw;
        mLastRaw   = raw;
        mPrimed    = true;
    }
    else if (raw >= mLastRaw) {
        // Forward progress. A pending candidate was a one-off glitch.
        if (mResetPending) {
            LOG_INFO("step counter: glitch read rejected (cand=%u)\n",
                     mResetCandRaw);
            mResetPending = false;
        }
        mLastRaw = raw;
    }
    else { // raw < mLastRaw
        if (!mResetPending) {
            // Suspected chip reset - hold one sample before acting on it.
            mResetPending = true;
            mResetCandRaw = raw;
        }
        else if (raw >= mResetCandRaw) {
            // Corroborated: the chip genuinely soft-reset. Bank the finished
            // epoch and re-base on the post-reset counter (zeroes on reset).
            mOffset      += (mLastRaw - mEpochBase);
            mEpochBase    = 0;
            mLastRaw      = raw;
            mResetPending = false;
            LOG_INFO("step counter: chip reset confirmed, total=%u\n",
                     getData());
        }
        else {
            // Still falling - re-arm the candidate with the lower value.
            mResetCandRaw = raw;
        }
    }

    if (getData() != before) {
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

uint32_t ImuSubSensorStepCounter::getData() const
{
    return mOffset + (mLastRaw - mEpochBase);
}

void ImuSubSensorStepCounter::publishData()
{
    const uint32_t value = getData();

    LOG_DEBUG("tx steps=%u\n", value);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.u[SDK::SensorDataParser::StepCounter::Field::STEP_COUNT] = value;
    mDriver.pushDataSample();
}

} // namespace Sensor
