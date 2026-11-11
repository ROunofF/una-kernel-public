/**
 ******************************************************************************
 * @file    SensorHeartRateMetricsDaily.cpp
 * @brief   Daily heart-rate metrics (AHR / RHR) sink sensor
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.HRMetricsDaily"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Daily/SensorHeartRateMetricsDaily.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateMetrics.hpp"

namespace Sensor
{

HeartRateMetricsDaily::HeartRateMetricsDaily()
    : mDriver(*this,
              SDK::Sensor::Type::HEART_RATE_METRICS,
              SDK::SensorDataParser::HeartRateMetrics::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& HeartRateMetricsDaily::getDriver()
{
    return mDriver;
}

float HeartRateMetricsDaily::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    return period;
}

void HeartRateMetricsDaily::sdcStop(Sensor::Driver* /*driver*/)
{
}

const char* HeartRateMetricsDaily::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Heart rate metrics";
}

void HeartRateMetricsDaily::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData();
}

void HeartRateMetricsDaily::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData();
    }
}

void HeartRateMetricsDaily::onDailyValuesChange(DailyHealth::DailyRecord record)
{
    const float ahr = static_cast<float>(record.averageHeartRate);
    const float rhr = static_cast<float>(record.restingHeartRate);

    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx ahr=%u rhr=%u\n",
              static_cast<uint32_t>(ahr),
              static_cast<uint32_t>(rhr));
    if (mAhr != ahr || mRhr != rhr) {
        mAhr       = ahr;
        mRhr       = rhr;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void HeartRateMetricsDaily::publishData()
{
    LOG_DEBUG("tx ahr=%u rhr=%u\n",
              static_cast<uint32_t>(mAhr),
              static_cast<uint32_t>(mRhr));
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.f[SDK::SensorDataParser::HeartRateMetrics::Field::AHR] = mAhr;
    sample.f[SDK::SensorDataParser::HeartRateMetrics::Field::RHR] = mRhr;
    mDriver.pushDataSample();
}

} // namespace Sensor
