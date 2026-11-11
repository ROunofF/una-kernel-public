/**
 ******************************************************************************
 * @file    SensorBatteryCharging.cpp
 * @brief   Battery charging-state sensor
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.BattChrg"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/SensorBatteryCharging.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"

namespace Sensor
{

BatteryCharging::BatteryCharging()
    : mDriver(*this,
              SDK::Sensor::Type::BATTERY_CHARGING,
              SDK::SensorDataParser::BatteryCharging::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
    , mPower(Power::GetInstance())
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& BatteryCharging::getDriver()
{
    return mDriver;
}

float BatteryCharging::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    // Seed the cache before attaching so the first callback change-checks
    // against the real current state. No lock needed: nothing else can
    // touch the cache until attachStatusCallback() runs.
    mConnected = mPower.isUsbDetected();
    mCharging  = mPower.isCharging();
    mPower.attachStatusCallback(this);

    return period;
}

void BatteryCharging::sdcStop(Sensor::Driver* /*driver*/)
{
    mPower.detachStatusCallback(this);
}

const char* BatteryCharging::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Battery charging";
}

void BatteryCharging::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void BatteryCharging::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void BatteryCharging::onUsbDetected(bool state)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx connected=%u\n", static_cast<uint32_t>(state));
    if (mConnected != state) {
        mConnected = state;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void BatteryCharging::onCharging(bool state)
{
    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx charging=%u\n", static_cast<uint32_t>(state));
    if (mCharging != state) {
        mCharging  = state;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void BatteryCharging::publishData()
{
    LOG_DEBUG("tx connected=%u charging=%u\n",
              static_cast<uint32_t>(mConnected),
              static_cast<uint32_t>(mCharging));
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.u[SDK::SensorDataParser::BatteryCharging::CONNECTED] = static_cast<uint32_t>(mConnected);
    sample.u[SDK::SensorDataParser::BatteryCharging::CHARGING]  = static_cast<uint32_t>(mCharging);
    mDriver.pushDataSample();
}

} // namespace Sensor
