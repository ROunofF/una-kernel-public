/**
 ******************************************************************************
 * @file    SensorActivityTimeDaily.cpp
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.ActTimeDaily"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Daily/SensorActivityTimeDaily.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp"

namespace Sensor
{

ActivityTimeDaily::ActivityTimeDaily()
    : mDriver(*this,
              SDK::Sensor::Type::ACTIVITY_TIME_DAILY,
              SDK::SensorDataParser::Activity::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ActivityTimeDaily::getDriver()
{
    return mDriver;
}

float ActivityTimeDaily::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    return period;
}

void ActivityTimeDaily::sdcStop(Sensor::Driver* /*driver*/)
{
}

const char* ActivityTimeDaily::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Daily activity time";
}

void ActivityTimeDaily::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void ActivityTimeDaily::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void ActivityTimeDaily::onDailyValuesChange(DailyHealth::DailyRecord record)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx minutes=%u\n", record.activityMinutes);
    if (mValue != record.activityMinutes) {
        mValue     = record.activityMinutes;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void ActivityTimeDaily::publishData()
{
    LOG_DEBUG("tx minutes=%u\n", mValue);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.u[SDK::SensorDataParser::Activity::Field::DURATION] = mValue;
    mDriver.pushDataSample();
}

} // namespace Sensor
