/**
 ******************************************************************************
 * @file    SensorBatteryLevel.hpp
 * @brief   Battery charge-level sensor
 ******************************************************************************
 */

#ifndef SENSOR_BATTERY_LEVEL_HPP
#define SENSOR_BATTERY_LEVEL_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "OS.hpp"
#include "Power.hpp"

#include <cstdint>

namespace Sensor
{

class BatteryLevel : public Interface::ISensor,
                     public Sensor::ISensorDriverCtrl,
                     public Power::StatusCallback
{
public:
    BatteryLevel();
    ~BatteryLevel() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // Power::StatusCallback
    void onBatteryLevelChanged(float level) override;

private:
    void publishData();

    Sensor::Driver mDriver;
    Power&         mPower;
    float          mLevel     = 0.0f;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_BATTERY_LEVEL_HPP
