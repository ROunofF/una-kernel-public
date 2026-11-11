/**
 ******************************************************************************
 * @file    ImuSubSensorGyroRaw.hpp
 * @brief   SubSensor for the gyroscope
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_GYRO_RAW_HPP
#define IMU_SUB_SENSOR_GYRO_RAW_HPP

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
    class ImuSubSensorGyroRaw : public Sensor::ISubSensor,
                                public Interface::ISensor
    {
    public:
        ImuSubSensorGyroRaw(Interface::IImu&           imu,
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

        /**
         * @brief Take a batch read from the FIFO.
         * @param data     Frames, oldest first.
         * @param count    Number of frames.
         * @param periodUs Step the part ran at for them, reported to the
         *                 listeners as the source step.
         */
        void addNewData(const Interface::IImu::SensorRawData* data, size_t count, uint32_t periodUs);

    private:
        Sensor::Driver    mDriver;
        ::Driver::SwTimer mTimer;
        Interface::IImu&  mImu;

        OS::Queue<Interface::IImu::SensorRawData, 1500> mQueue;

        /// The step each reading held in mQueue was taken at.
        Sensor::SourceStepTrack mSteps;
        uint32_t mLastTimestamp;
        bool     mLastTimestampValid;

        static constexpr float mMinPeriod = 0.625f; // In ms

        /// Latency asked for the stream, ms; 0 for none. Set on the connection
        /// path and read by the refresh on the sweep thread; it orders nothing
        /// else, so every access is relaxed.
        std::atomic<uint32_t> mLatency { 0 };
    };

} // namespace Sensor

#endif // IMU_SUB_SENSOR_GYRO_RAW_HPP
