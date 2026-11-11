/**
 ******************************************************************************
 * @file    SensorStepCounterDaily.cpp
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.StepCntDaily"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Daily/SensorStepCounterDaily.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"

namespace Sensor
{

StepCounterDaily::StepCounterDaily()
    : mDriver(*this,
              SDK::Sensor::Type::STEP_COUNTER_DAILY,
              SDK::SensorDataParser::StepCounter::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& StepCounterDaily::getDriver()
{
    return mDriver;
}

float StepCounterDaily::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    return period;
}

void StepCounterDaily::sdcStop(Sensor::Driver* /*driver*/)
{
}

const char* StepCounterDaily::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Daily step counter";
}

void StepCounterDaily::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void StepCounterDaily::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void StepCounterDaily::onDailyValuesChange(DailyHealth::DailyRecord record)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx steps=%u\n", record.steps);
    if (mValue != record.steps) {
        mValue     = record.steps;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void StepCounterDaily::publishData()
{
    LOG_DEBUG("tx steps=%u\n", mValue);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.u[SDK::SensorDataParser::StepCounter::Field::STEP_COUNT] = mValue;
    mDriver.pushDataSample();
}

} // namespace Sensor
