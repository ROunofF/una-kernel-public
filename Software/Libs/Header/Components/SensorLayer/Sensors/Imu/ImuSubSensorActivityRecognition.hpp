/**
 ******************************************************************************
 * @file    ImuSubSensorActivityRecognition.hpp
 * @brief   Activity-recognition sub-sensor for BMI270
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_ACTIVITY_RECOGNITION_HPP
#define IMU_SUB_SENSOR_ACTIVITY_RECOGNITION_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "IImu.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class ImuSubSensorActivityRecognition : public Sensor::ISubSensor,
                                        public Interface::ISensor
{
public:
    ImuSubSensorActivityRecognition(Interface::IImu&           imu,
                                    Sensor::ISensorDriverCtrl& ctrl);
    ~ImuSubSensorActivityRecognition() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void handleActivityDetected(SDK::SensorDataParser::ActivityRecognition::Activity id,
                                uint64_t                                            timestamp);

private:
    struct Data {
        uint64_t ts         = 0;   //// us, from BMI270 (system time on newConnection)
        uint8_t  activity   = 0;
        uint8_t  confidence = 0;
    };

    void publishData();

    Sensor::Driver mDriver;
    Interface::IImu& mImu;
    Data           mData;
    bool           mActive = false;
    bool           mDirty  = false;
    OS::Mutex      mDataMutex;
};

} // namespace Sensor

#endif // IMU_SUB_SENSOR_ACTIVITY_RECOGNITION_HPP
