/**
 ******************************************************************************
 * @file    BodySubSensorTouch.hpp
 * @brief   Touch (worn / unworn) sub-sensor
 ******************************************************************************
 */

#ifndef BODY_SUB_SENSOR_TOUCH_HPP
#define BODY_SUB_SENSOR_TOUCH_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class BodySubSensorTouch : public Sensor::ISubSensor,
                           public Interface::ISensor
{
public:
    BodySubSensorTouch(Sensor::ISensorDriverCtrl& ctrl);
    ~BodySubSensorTouch() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void setTouch(bool value);

private:
    // touch / no-touch are paired transitions; the queue keeps every one
    // in order so a burst between refreshes cannot invert the state.
    struct TouchEvent {
        bool     touched;
        uint32_t timestamp;
    };

    void publishData(const TouchEvent& event);

    Sensor::Driver           mDriver;
    OS::Queue<TouchEvent, 3> mQueue;
    bool                     mActive  = false;
    bool                     mTouched = false;
    OS::Mutex                mDataMutex;
};

} // namespace Sensor

#endif // BODY_SUB_SENSOR_TOUCH_HPP
