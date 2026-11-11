/**
 ******************************************************************************
 * @file    SensorActivityTime.hpp
 * @brief   Passive sink sensor that exposes cumulative active minutes
 *          from boot.
 *
 *          Subscribes as an Activity::IDurationListener and forwards
 *          the value to its own SensorLayer driver channel
 *          (SDK::Sensor::Type::ACTIVITY_TIME). UI / BLE consumers that
 *          want a monotonic-from-boot signal (e.g. workout-mode apps)
 *          read it through the standard SensorManager subscription.
 ******************************************************************************
 */

#ifndef SENSOR_ACTIVITY_TIME_HPP
#define SENSOR_ACTIVITY_TIME_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "Activity/IDurationListener.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class ActivityTime : public Interface::ISensor,
                     public Sensor::ISensorDriverCtrl,
                     public Activity::IDurationListener
{
public:
    ActivityTime();
    ~ActivityTime() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // Activity::IDurationListener
    void onActivityDurationChanged(uint32_t minutesSinceBoot) override;

private:
    void publishData();

    Sensor::Driver mDriver;
    uint32_t       mValue     = 0;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_ACTIVITY_TIME_HPP
