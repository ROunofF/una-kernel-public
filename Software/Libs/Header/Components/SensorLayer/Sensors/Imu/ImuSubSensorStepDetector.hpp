/**
 ******************************************************************************
 * @file    ImuSubSensorStepDetector.hpp
 * @brief   Step-detector sub-sensor for BMI270
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_STEP_DETECTOR_HPP
#define IMU_SUB_SENSOR_STEP_DETECTOR_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "IImu.hpp"
#include "OS.hpp"

#include <atomic>
#include <cstdint>

namespace Sensor
{

class ImuSubSensorStepDetector : public Sensor::ISubSensor,
                                 public Interface::ISensor
{
public:
    ImuSubSensorStepDetector(Interface::IImu&           imu,
                             Sensor::ISensorDriverCtrl& ctrl);
    ~ImuSubSensorStepDetector() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void handleStepDetected(uint64_t timestamp);

private:
    void publishData(uint64_t timestampUs);

    Sensor::Driver mDriver;
    Interface::IImu& mImu;
    std::atomic<bool> mActive{false};

    OS::Queue<uint64_t, 5> mQueue;
};

} // namespace Sensor

#endif // IMU_SUB_SENSOR_STEP_DETECTOR_HPP
