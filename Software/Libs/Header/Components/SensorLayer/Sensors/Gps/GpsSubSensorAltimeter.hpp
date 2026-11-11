/**
 ******************************************************************************
 * @file    GpsSubSensorAltimeter.hpp
 * @brief   SubSensor for the GPS Altimeter
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __GPS_SUB_SENSOR_ALTIMETER_HPP
#define __GPS_SUB_SENSOR_ALTIMETER_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"
#include "IGps.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class GpsSubSensorAltimeter : public Sensor::ISubSensor,
                                  public Interface::ISensor
    {
    public:
        GpsSubSensorAltimeter(Sensor::ISensorDriverCtrl& ctrl);

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

        void setAltitude(float value);

    private:
        Sensor::Driver    mDriver;
        Sensor::SourceTick mTick;
        float             mData;
        OS::Mutex         mDataMutex;
    }; /* class GpsSubSensorAltimeter */

} // namespace Sensor

#endif /* __GPS_SUB_SENSOR_ALTIMETER_HPP */
