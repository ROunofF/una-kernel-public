/**
 ******************************************************************************
 * @file    ISubSensor.hpp
 * @brief   Base interface for sub-sensors
 ******************************************************************************
 */

#ifndef I_SUB_SENSOR_HPP
#define I_SUB_SENSOR_HPP

#include "SensorLayer/Core/SensorDriver.hpp"

namespace Sensor
{

class ISubSensor
{
public:
    ISubSensor()          = default;
    virtual ~ISubSensor() = default;

    virtual Sensor::Driver& getDriver()         = 0;
    virtual bool            isActive()          = 0;
    virtual float           start(float period) = 0;
    virtual void            stop()              = 0;
    virtual const char*     getDescription()    = 0;

    virtual void newConnection() {}

    // The tightest delivery latency among the listeners, in ms; 0 when the
    // last one has left. It matters to a sub-sensor that gathers readings
    // before handing them over, or that draws on one that does.
    virtual void updateLatency(uint32_t latency) { (void) latency; }

    // Period handling is meaningful only for periodic sub-sensors.
    // Event-based ones deliver on their own events and can leave these
    // at the defaults instead of implementing them.
    virtual float updatePeriod(float period) { return period; }
    virtual float getMinPeriod()             { return 0.0f; }
    virtual float getCurrentPeriod()         { return 0.0f; }
};

} // namespace Sensor

#endif // I_SUB_SENSOR_HPP
