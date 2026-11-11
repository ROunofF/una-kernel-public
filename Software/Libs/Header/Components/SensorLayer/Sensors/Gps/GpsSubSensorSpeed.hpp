/**
 ******************************************************************************
 * @file    GpsSubSensorSpeed.hpp
 * @brief   SubSensor for the GPS Speed
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __GPS_SUB_SENSOR_SPEED_HPP
#define __GPS_SUB_SENSOR_SPEED_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"
#include "IGps.hpp"

#include <cstdint>

namespace Sensor
{
    class GpsSubSensorSpeed : public Sensor::ISubSensor,
                              public Interface::ISensor
    {
    public:
        GpsSubSensorSpeed(Sensor::ISensorDriverCtrl& ctrl);

        //// ISubSensor
        Sensor::Driver& getDriver()                override;
        bool            isActive()                 override;
        float           start(float period)        override;
        void            stop()                     override;
        float           updatePeriod(float period) override;
        float           getMinPeriod()             override;
        float           getCurrentPeriod()         override;
        const char*     getDescription()           override;

        //// ISensor
        void sensorRefresh() override;

    private:
        Interface::IGps&  mGps;
        Sensor::Driver    mDriver;
        Sensor::SourceTick mTick;
    }; /* class GpsSubSensorSpeed */

} // namespace Sensor

#endif /* __GPS_SUB_SENSOR_SPEED_HPP */
