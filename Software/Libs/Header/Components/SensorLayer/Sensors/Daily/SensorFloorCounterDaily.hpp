/**
 ******************************************************************************
 * @file    SensorFloorCounterDaily.hpp
 * @brief   Passive sink sensor that exposes today's floor counters.
 *
 *          Subscribes as a DailyHealth::Service::Callback and forwards
 *          DailyRecord.floorsUp / floorsDown to its own SensorLayer
 *          driver channel (SDK::Sensor::Type::FLOOR_COUNTER_DAILY).
 ******************************************************************************
 */

#ifndef SENSOR_FLOOR_COUNTER_DAILY_HPP
#define SENSOR_FLOOR_COUNTER_DAILY_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "DailyHealth/Service.hpp"
#include "DailyHealth/Types.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class FloorCounterDaily : public Interface::ISensor,
                          public Sensor::ISensorDriverCtrl,
                          public DailyHealth::Service::Callback
{
public:
    FloorCounterDaily();
    ~FloorCounterDaily() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // DailyHealth::Service::Callback
    void onDailyValuesChange(DailyHealth::DailyRecord record) override;

private:
    void publishData();

    Sensor::Driver mDriver;
    uint32_t       mUp        = 0;
    uint32_t       mDown      = 0;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_FLOOR_COUNTER_DAILY_HPP
