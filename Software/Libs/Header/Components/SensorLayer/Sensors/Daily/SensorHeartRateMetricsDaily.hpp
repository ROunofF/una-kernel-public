/**
 ******************************************************************************
 * @file    SensorHeartRateMetricsDaily.hpp
 * @brief   Daily heart-rate metrics (AHR / RHR) sink sensor
 ******************************************************************************
 */

#ifndef SENSOR_HEART_RATE_METRICS_DAILY_HPP
#define SENSOR_HEART_RATE_METRICS_DAILY_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "DailyHealth/Service.hpp"
#include "DailyHealth/Types.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class HeartRateMetricsDaily : public Interface::ISensor,
                              public Sensor::ISensorDriverCtrl,
                              public DailyHealth::Service::Callback
{
public:
    HeartRateMetricsDaily();
    ~HeartRateMetricsDaily() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // DailyHealth::Service::Callback: AHR / RHR are computed by DailyHealth
    // on every minute tick from the per-minute HR matrix; this is the
    // channel that delivers them here.
    void onDailyValuesChange(DailyHealth::DailyRecord record) override;

private:
    void publishData();

    Sensor::Driver mDriver;
    float          mAhr       = 0.0f;
    float          mRhr       = 0.0f;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_HEART_RATE_METRICS_DAILY_HPP
