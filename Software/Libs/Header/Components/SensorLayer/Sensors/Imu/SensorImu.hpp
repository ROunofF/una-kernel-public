/**
 ******************************************************************************
 * @file    SensorImu.hpp
 * @brief   Sensor IMU
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_IMU_HPP
#define SENSOR_IMU_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "IImu.hpp"

#include "SensorLayer/Sensors/Imu/ImuSubSensorAccel.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorAccelRaw.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorGyro.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorGyroRaw.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorFusion.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorFusionRaw.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorStepCounter.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorStepDetector.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorMotionDetect.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorActivityRecognition.hpp"
#include "SensorLayer/Sensors/Imu/ImuSubSensorWristMotion.hpp"

#include <cstdint>
#include <array>

namespace Sensor
{
    class Imu : public Sensor::ISensorDriverCtrl,
                public Interface::IImu::SensorCallback
    {
    public:
        explicit Imu(Interface::IImu& imu);

        /// The part the sub-sensors drive.
        Interface::IImu& getImu();

        Sensor::Driver& getDriverAccel();
        Sensor::Driver& getDriverAccelRaw();
        Sensor::Driver& getDriverGyro();
        Sensor::Driver& getDriverGyroRaw();
        Sensor::Driver& getDriverFusion();
        Sensor::Driver& getDriverFusionRaw();
        Sensor::Driver& getDriverStepCounter();
        Sensor::Driver& getDriverStepDetector();
        Sensor::Driver& getDriverMotionDetect();
        Sensor::Driver& getDriverActivityRecognition();
        Sensor::Driver& getDriverWristMotion();

        // ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        void        sdcUpdateLatency(Sensor::Driver* driver, uint32_t latency) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;
        void        sdcNewConnection(Sensor::Driver* driver)              override;

    private:
        void onAccelRaw(const Interface::IImu::SensorRawData* data,
                        size_t                                count,
                        float                                 scale,
                        uint32_t                              periodUs) override;


        void onGyroRaw(const Interface::IImu::SensorRawData* data,
                       size_t                                count,
                       float                                 scale,
                       uint32_t                              periodUs) override;

        void onSyncRaw(const Interface::IImu::SyncSampleData* data,
                       size_t                                 count,
                       float                                  accelScale,
                       float                                  gyroScale,
                       uint32_t                               periodUs) override;

        void onActivity(Interface::IImu::ActivityType type, uint64_t timestamp) override;
        void onNoMotion(uint64_t timestamp)                                override;
        void onMotion(uint64_t timestamp)                                  override;
        void onSigMotion(uint64_t timestamp)                               override;
        void onStepCounter(uint32_t steps, uint64_t timestamp)             override;
        void onStepDetector(uint64_t timestamp)                            override;
        void onWristWakeup(uint64_t timestamp)                             override;

        uint8_t getActiveCount();
        Sensor::ISubSensor* getSubSensor(Sensor::Driver* driver);

        Interface::IImu&                 mImu;
        Sensor::ImuSubSensorAccelRaw     mAccelRaw;
        Sensor::ImuSubSensorAccel        mAccel;
        Sensor::ImuSubSensorGyroRaw      mGyroRaw;
        Sensor::ImuSubSensorGyro         mGyro;
        Sensor::ImuSubSensorFusionRaw    mFusionRaw;
        Sensor::ImuSubSensorFusion       mFusion;
        Sensor::ImuSubSensorStepCounter  mStepCounter;
        Sensor::ImuSubSensorStepDetector     mStepDetector;
        Sensor::ImuSubSensorMotionDetect     mMotionDetector;
        Sensor::ImuSubSensorActivityRecognition mActivity;
        Sensor::ImuSubSensorWristMotion  mWristMotion;

        std::array<Sensor::ISubSensor*, 11> mSubSensors;
    }; /* class BMI270 */

} // namespace Sensor

#endif // SENSOR_IMU_HPP
