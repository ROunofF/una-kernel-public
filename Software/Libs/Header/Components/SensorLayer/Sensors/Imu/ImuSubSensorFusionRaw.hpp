/**
 ******************************************************************************
 * @file    ImuSubSensorFusionRaw.hpp
 * @brief   SubSensor for the FUSION RAW
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_FUSION_RAW_HPP
#define IMU_SUB_SENSOR_FUSION_RAW_HPP

#include <atomic>
#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SourceStepTrack.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "IImu.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class ImuSubSensorFusionRaw : public Sensor::ISubSensor,
                                  public Interface::ISensor,
                                  public SDK::Interface::ISensorDataListener
    {
    public:
        ImuSubSensorFusionRaw(Sensor::ISensorDriverCtrl& ctrl);

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
         * @brief Take a batch of paired frames read from the FIFO.
         * @param data     Frames, oldest first.
         * @param count    Number of frames.
         * @param periodUs Step both streams ran at for them, reported to the
         *                 listeners as the source step.
         */
        void addNewData(const Interface::IImu::SyncSampleData* data, size_t count, uint32_t periodUs);

    private:
        void onSdlNewData(uint16_t                 handle,
                          const SDK::Sensor::Data* base,
                          uint16_t                 count,
                          uint16_t                 stride) override;

    private:
        static constexpr float mMinPeriod = 0.625f; // In ms

        /// Latency asked of the raw streams until a listener states its own.
        static constexpr uint32_t kDefaultLatency = 200; // In ms

        Sensor::Driver     mDriver;
        ::Driver::SwTimer  mTimer;
        Sensor::Connection mDriverXL;
        Sensor::Connection mDriverGY;

        OS::Queue<Interface::IImu::SyncSampleData, 1500> mQueue;

        /// The step each reading held in mQueue was taken at.
        Sensor::SourceStepTrack mSteps;

        float    mPeriod;   ///< Period the raw streams run at, ms
        /// Latency asked of the raw streams, ms. Set on the connection path and read
        /// by the refresh on the sweep thread; it orders nothing else, so every
        /// access is relaxed.
        std::atomic<uint32_t> mLatency;
    };

} // namespace Sensor

#endif // IMU_SUB_SENSOR_FUSION_RAW_HPP
