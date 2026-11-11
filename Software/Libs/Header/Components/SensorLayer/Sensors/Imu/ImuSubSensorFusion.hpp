/**
 ******************************************************************************
 * @file    ImuSubSensorFusion.hpp
 * @brief   SubSensor for the Fusion
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_FUSION_HPP
#define IMU_SUB_SENSOR_FUSION_HPP

#include <atomic>
#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SourceStepTrack.hpp"
#include "IImu.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class ImuSubSensorFusion : public Sensor::ISubSensor,
                               public Interface::ISensor,
                               public SDK::Interface::ISensorDataListener
    {
    public:
        struct Data {
            uint64_t ts;
            float    accelX;        ///< Axis accelerometer X
            float    accelY;        ///< Axis accelerometer Y
            float    accelZ;        ///< Axis accelerometer Z
            float    gyroX;         ///< Axis gyroscope X
            float    gyroY;         ///< Axis gyroscope Y
            float    gyroZ;         ///< Axis gyroscope Z
        };

        ImuSubSensorFusion(Sensor::Driver&            driverFusionRaw,
                           Sensor::ISensorDriverCtrl& ctrl);

        //// ISubSensor
        Sensor::Driver& getDriver()                override;
        bool            isActive()                 override;
        float           start(float period)        override;
        void            stop()                     override;
        float           updatePeriod(float period) override;
        void            updateLatency(uint32_t latency) override;
        float           getMinPeriod()             override;
        float           getCurrentPeriod()         override;
        const char*     getDescription()           override;

        //// ISensor
        void sensorRefresh() override;

        //// ISensorDataListener
        void onSdlNewData(uint16_t                 handle,
                          const SDK::Sensor::Data* base,
                          uint16_t                 count,
                          uint16_t                 stride) override;

    private:
        static constexpr float mMinPeriod        = 0.625; // In ms
        static constexpr float kGravity          = 9.80665f;
        static constexpr float kRawInt16Scale    = 32767.0f;
        static constexpr float kAccelFullScale   = 8.0f;
        static constexpr float mScaleAccel       = kAccelFullScale * kGravity / kRawInt16Scale;
        static constexpr float kGyroFullScaleDps = 2000.0f;
        static constexpr float mScaleGyro        = kGyroFullScaleDps / kRawInt16Scale;

        Sensor::Driver        mDriver;
        ::Driver::SwTimer     mTimer;
        Sensor::Driver&       mDriverFusionRaw;

        OS::Queue<Data, 1600> mQueue;

        /// The step each reading held in mQueue was taken at.
        Sensor::SourceStepTrack mSteps;

        /// Latency asked of the raw stream until a listener states its own.
        static constexpr uint32_t kDefaultLatency = 200; // In ms

        float    mPeriod;   ///< Period the raw stream runs at, ms
        /// mPeriod in us for the refresh on the sweep thread, which reads it
        /// with the raw stream's step to know the step of its own; relaxed.
        std::atomic<uint32_t> mPeriodUs;
        /// Latency asked of the raw stream, ms. Set on the connection path and read
        /// by the refresh on the sweep thread; it orders nothing else, so every
        /// access is relaxed.
        std::atomic<uint32_t> mLatency;
    };

} // namespace Sensor

#endif // IMU_SUB_SENSOR_FUSION_HPP
