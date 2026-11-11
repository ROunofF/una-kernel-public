/**
 ******************************************************************************
 * @file    SensorBatteryMetrics.cpp
 * @brief   Sensor Battery Metrics
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.BattMetrics"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/SensorBatteryMetrics.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"
#include "System.hpp"
#include "OS.hpp"

#include <inttypes.h>

namespace Sensor
{

BatteryMetrics::BatteryMetrics(FuelGauge& fg)
    : mFuelGauge(fg)
    , mDriver(*this,
              SDK::Sensor::Type::BATTERY_METRICS,
              SDK::SensorDataParser::BatteryMetrics::getFieldsNumber(),
              *this)
    , mTick()
{
    mDriver.setSourceTick(&mTick);
}

Sensor::Driver& BatteryMetrics::getDriver()
{
    return mDriver;
}

float BatteryMetrics::sdcStart(Sensor::Driver* driver, float period)
{
    (void) driver;

    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());

    return period;
}

void BatteryMetrics::sdcStop(Sensor::Driver* driver)
{
    (void) driver;

    mTick.stop();

    LOG_INFO("stopped\n");
}

float BatteryMetrics::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    (void) driver;

    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());

    return period;
}

float BatteryMetrics::sdcGetMinPeriod(Sensor::Driver* driver)
{
    (void) driver;

    return 1000.0f;
}

const char* BatteryMetrics::sdcGetDescription(Sensor::Driver* driver)
{
    (void) driver;

    return "Battery metrics";
}

void BatteryMetrics::sensorRefresh()
{
    if (!mDriver.tickDue()) {
        return;
    }

    LOG_DEBUG("ENTRY\n");

    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(OS::GetTimeMs());
    sample.f[SDK::SensorDataParser::BatteryMetrics::VOLTAGE]         = mFuelGauge.getBatteryVoltage();
    sample.f[SDK::SensorDataParser::BatteryMetrics::CURRENT]         = mFuelGauge.getCurrent();
    sample.f[SDK::SensorDataParser::BatteryMetrics::AVERAGE_CURRENT] = mFuelGauge.getAvgCurrent();
    sample.f[SDK::SensorDataParser::BatteryMetrics::CAPACITY]        = mFuelGauge.getCapacity();
    sample.f[SDK::SensorDataParser::BatteryMetrics::DESIGN_CAPACITY] = mFuelGauge.getFullCapacity();
    mDriver.pushDataSample();

    LOG_DEBUG("EXIT\n");
}

} // namespace Sensor
