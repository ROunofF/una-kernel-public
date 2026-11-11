/**
 ******************************************************************************
 * @file    SensorActivityTimeDaily.hpp
 * @brief   Passive sink sensor that exposes today's active minutes.
 *
 *          Subscribes as a DailyHealth::Service::Callback and forwards
 *          DailyRecord.activityMinutes to its own SensorLayer driver
 *          channel (SDK::Sensor::Type::ACTIVITY_TIME_DAILY, which is
 *          a value-level alias of SDK::Sensor::Type::ACTIVITY).
 ******************************************************************************
 */

#ifndef SENSOR_ACTIVITY_TIME_DAILY_HPP
#define SENSOR_ACTIVITY_TIME_DAILY_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "DailyHealth/Service.hpp"
#include "DailyHealth/Types.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class ActivityTimeDaily : public Interface::ISensor,
                          public Sensor::ISensorDriverCtrl,
                          public DailyHealth::Service::Callback
{
public:
    ActivityTimeDaily();
    ~ActivityTimeDaily() override = default;

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
    uint16_t       mValue     = 0;
    uint32_t       mTimestamp = 0;
    bool           mDirty     = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // SENSOR_ACTIVITY_TIME_DAILY_HPP
