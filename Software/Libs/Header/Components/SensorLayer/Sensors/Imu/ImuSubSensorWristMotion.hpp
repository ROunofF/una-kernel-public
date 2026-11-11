/**
 ******************************************************************************
 * @file    ImuSubSensorWristMotion.hpp
 * @brief   Wrist-motion sub-sensor for BMI270
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_WRIST_MOTION_HPP
#define IMU_SUB_SENSOR_WRIST_MOTION_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "IImu.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class ImuSubSensorWristMotion : public Sensor::ISubSensor,
                                public Interface::ISensor
{
public:
    ImuSubSensorWristMotion(Interface::IImu&           imu,
                            Sensor::ISensorDriverCtrl& ctrl);
    ~ImuSubSensorWristMotion() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void handleWristMotion(uint64_t timestamp);

private:
    void publishData();

    Sensor::Driver mDriver;
    Interface::IImu& mImu;
    bool           mActive    = false;
    bool           mDirty     = false;
    uint64_t       mTimestamp = 0;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // IMU_SUB_SENSOR_WRIST_MOTION_HPP
