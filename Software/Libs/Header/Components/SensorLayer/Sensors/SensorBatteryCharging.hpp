/**
 ******************************************************************************
 * @file    SensorBatteryCharging.hpp
 * @brief   Battery charging-state sensor
 ******************************************************************************
 */

#ifndef SENSOR_BATTERY_CHARGING_HPP
#define SENSOR_BATTERY_CHARGING_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "OS.hpp"
#include "Power.hpp"

#include <cstdint>

namespace Sensor
{

class BatteryCharging : public Interface::ISensor,
                        public Sensor::ISensorDriverCtrl,
                        public Power::StatusCallback
{
public:
    BatteryCharging();
    ~BatteryCharging() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // Power::StatusCallback
    void onUsbDetected(bool state) override;
    void onCharging(bool state)    override;

private:
    void publishData();

    Sensor::Driver mDriver;
    Power&         mPower;
    bool           mConnected = false;
    bool           mCharging  = false;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_BATTERY_CHARGING_HPP
