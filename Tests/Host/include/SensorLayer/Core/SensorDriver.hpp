/**
 * Host stand-in for Sensor::Driver.
 *
 * Sensor::DataQueue asks its driver for three things: the delivery mode, the
 * handle it passes to the listener and the source's nominal step. The real
 * driver brings the RTOS and the sensor manager with it; this one brings those
 * answers, so the queue itself can be built and tested on the host as it is.
 */

#ifndef HOST_SENSOR_DRIVER_HPP
#define HOST_SENSOR_DRIVER_HPP

#include <cstdint>

namespace Sensor
{

class Driver
{
public:
    enum class Mode { PERIODIC_BASED, EVENT_BASED };

    Mode getMode() const
    {
        return mode;
    }

    uint8_t getHandle()
    {
        return handle;
    }

    uint32_t getSourceStepUs() const
    {
        return sourceStepUs;
    }

    Mode     mode         = Mode::PERIODIC_BASED;
    uint8_t  handle       = 7;
    uint32_t sourceStepUs = 0;
};

} // namespace Sensor

#endif // HOST_SENSOR_DRIVER_HPP
