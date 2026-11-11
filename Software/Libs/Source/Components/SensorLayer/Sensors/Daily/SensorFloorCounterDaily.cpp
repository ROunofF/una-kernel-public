/**
 ******************************************************************************
 * @file    SensorFloorCounterDaily.cpp
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.FloorCntDaily"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Daily/SensorFloorCounterDaily.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp"

namespace Sensor
{

FloorCounterDaily::FloorCounterDaily()
    : mDriver(*this,
              SDK::Sensor::Type::FLOOR_COUNTER_DAILY,
              SDK::SensorDataParser::FloorCounter::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& FloorCounterDaily::getDriver()
{
    return mDriver;
}

float FloorCounterDaily::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    return period;
}

void FloorCounterDaily::sdcStop(Sensor::Driver* /*driver*/)
{
}

const char* FloorCounterDaily::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Daily floor counter";
}

void FloorCounterDaily::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void FloorCounterDaily::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void FloorCounterDaily::onDailyValuesChange(DailyHealth::DailyRecord record)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx up=%u down=%u\n", record.floorsUp, record.floorsDown);
    if (mUp != record.floorsUp || mDown != record.floorsDown) {
        mUp        = record.floorsUp;
        mDown      = record.floorsDown;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void FloorCounterDaily::publishData()
{
    LOG_DEBUG("tx up=%u down=%u\n", mUp, mDown);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.i[SDK::SensorDataParser::FloorCounter::FLOORS_UP]   = static_cast<int32_t>(mUp);
    sample.i[SDK::SensorDataParser::FloorCounter::FLOORS_DOWN] = static_cast<int32_t>(mDown);
    mDriver.pushDataSample();
}

} // namespace Sensor
