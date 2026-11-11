/**
 ******************************************************************************
 * @file    SensorBatteryLevel.cpp
 * @brief   Battery charge-level sensor
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.BattLevel"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/SensorBatteryLevel.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"

namespace Sensor
{

BatteryLevel::BatteryLevel()
    : mDriver(*this,
              SDK::Sensor::Type::BATTERY_LEVEL,
              SDK::SensorDataParser::BatteryLevel::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
    , mPower(Power::GetInstance())
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& BatteryLevel::getDriver()
{
    return mDriver;
}

float BatteryLevel::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    // Seed the cache before attaching so the first callback change-checks
    // against the real current level. No lock needed: nothing else can
    // touch the cache until attachStatusCallback() runs.
    mLevel = mPower.getBatteryLevel();
    mPower.attachStatusCallback(this);

    return period;
}

void BatteryLevel::sdcStop(Sensor::Driver* /*driver*/)
{
    mPower.detachStatusCallback(this);
}

const char* BatteryLevel::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Battery level";
}

void BatteryLevel::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void BatteryLevel::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void BatteryLevel::onBatteryLevelChanged(float level)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx level=%u\n", static_cast<uint32_t>(level));
    if (mLevel != level) {
        mLevel     = level;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void BatteryLevel::publishData()
{
    LOG_DEBUG("tx level=%u\n", static_cast<uint32_t>(mLevel));
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.f[SDK::SensorDataParser::BatteryLevel::LEVEL] = mLevel;
    mDriver.pushDataSample();
}

} // namespace Sensor
