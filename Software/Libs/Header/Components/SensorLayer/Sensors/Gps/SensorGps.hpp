/**
 ******************************************************************************
 * @file    SensorGps.hpp
 * @brief   Sensor for the GPS
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SENSOR_GPS_HPP
#define __SENSOR_GPS_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SensorLayer/Sensors/Gps/GpsSubSensorLocation.hpp"
#include "SensorLayer/Sensors/Gps/GpsSubSensorSpeed.hpp"
#include "SensorLayer/Sensors/Gps/GpsSubSensorDistance.hpp"
#include "SensorLayer/Sensors/Gps/GpsSubSensorAltimeter.hpp"
#include "SwTimer.hpp"
#include "IGps.hpp"
#include "OS.hpp"

#include <cstdint>
#include <array>

namespace Sensor
{
    class Gps : public Sensor::ISensorDriverCtrl,
                public Interface::IGps::Callback
    {
    public:
        Gps();
        
        Sensor::Driver& getDriverLocation();
        Sensor::Driver& getDriverSpeed();
        Sensor::Driver& getDriverDistance();
        Sensor::Driver& getDriverAltimeter();

        //// ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

        //// IGps
        void onGpsLocation(Interface::IGps::LocationInfo location) override;

    private:
        uint8_t getActiveCount();
        float   getMinPeriod();
        void    updateGpsPeriod();

        Sensor::ISubSensor* getSubSensor(Sensor::Driver* drive);

        Interface::IGps&              mGps;
        Sensor::GpsSubSensorLocation  mLocation;
        Sensor::GpsSubSensorSpeed     mSpeed;
        Sensor::GpsSubSensorDistance  mDistance;
        Sensor::GpsSubSensorAltimeter mAltimeter;
        uint32_t                      mGpsPeriod;

        std::array<Sensor::ISubSensor*, 4> mSubSensors;

    }; /* class StubPedo */

} /* namespace Sensor */

#endif /* __SENSOR_GPS_HPP */
