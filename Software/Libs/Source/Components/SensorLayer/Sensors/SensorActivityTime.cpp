/**
 ******************************************************************************
 * @file    SensorActivityTime.cpp
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.ActTime"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/SensorActivityTime.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp"

namespace Sensor
{

ActivityTime::ActivityTime()
    : mDriver(*this,
              SDK::Sensor::Type::ACTIVITY_TIME,
              SDK::SensorDataParser::Activity::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ActivityTime::getDriver()
{
    return mDriver;
}

float ActivityTime::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    return period;
}

void ActivityTime::sdcStop(Sensor::Driver* /*driver*/)
{
}

const char* ActivityTime::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Activity time since boot";
}

void ActivityTime::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void ActivityTime::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void ActivityTime::onActivityDurationChanged(uint32_t minutesSinceBoot)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx duration=%u\n", minutesSinceBoot);
    if (mValue != minutesSinceBoot) {
        mValue     = minutesSinceBoot;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void ActivityTime::publishData()
{
    LOG_DEBUG("tx duration=%u\n", mValue);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.u[SDK::SensorDataParser::Activity::Field::DURATION] = mValue;
    mDriver.pushDataSample();
}

} // namespace Sensor
