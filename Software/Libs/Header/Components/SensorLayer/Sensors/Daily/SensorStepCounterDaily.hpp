/**
 ******************************************************************************
 * @file    SensorStepCounterDaily.hpp
 * @brief   Passive sink sensor that exposes today's step count.
 *
 *          Subscribes as a DailyHealth::Service::Callback and forwards
 *          DailyRecord.steps to its own SensorLayer driver channel
 *          (SDK::Sensor::Type::STEP_COUNTER_DAILY). UI / BLE consumers
 *          read the value through the standard SensorManager
 *          subscription path.
 ******************************************************************************
 */

#ifndef SENSOR_STEP_COUNTER_DAILY_HPP
#define SENSOR_STEP_COUNTER_DAILY_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "DailyHealth/Service.hpp"
#include "DailyHealth/Types.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class StepCounterDaily : public Interface::ISensor,
                         public Sensor::ISensorDriverCtrl,
                         public DailyHealth::Service::Callback
{
public:
    StepCounterDaily();
    ~StepCounterDaily() override = default;

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
    uint32_t       mValue     = 0;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_STEP_COUNTER_DAILY_HPP
