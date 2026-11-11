/**
 ******************************************************************************
 * @file    BodySubSensorHeartRateEx.hpp
 * @brief   SubSensor for HEART_RATE_EX (opt-in multi-source heart rate).
 *
 * Publishes the arbitrated reading plus the source that was chosen and the raw
 * optical and external readings, so an app can show source-driven status and
 * log internal (PPG), external (strap), and arbitrated HR as separate series.
 * SensorBody feeds it the (already wrist-gated) values from publishHr(); this
 * class is a plain periodic publisher - no touch/accel machinery of its own.
 ******************************************************************************
 */

#ifndef __BODY_SUB_SENSOR_HEART_RATE_EX_HPP
#define __BODY_SUB_SENSOR_HEART_RATE_EX_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Sensors/Body/HrSourceArbiter.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class BodySubSensorHeartRateEx : public Sensor::ISubSensor,
                                     public Interface::ISensor
    {
    public:
        BodySubSensorHeartRateEx(Sensor::ISensorDriverCtrl& ctrl);

        //// ISubSensor
        Sensor::Driver& getDriver()                override;
        bool            isActive()                 override;
        float           start(float period)        override;
        void            stop()                     override;
        float           updatePeriod(float period) override;
        float           getMinPeriod()             override;
        float           getCurrentPeriod()         override;
        const char*     getDescription()           override;

        //// ISensor
        void sensorRefresh() override;

        /**
         * @brief   Feed the latest multi-source reading. Values are final
         *          (SensorBody has already applied wrist-gating); a 0 bpm means
         *          that source had no usable reading this tick.
         */
        void setData(uint8_t arbBpm, uint8_t arbTrust, HrSource source,
                     uint8_t optBpm, uint8_t optTrust,
                     uint8_t extBpm, uint8_t extTrust);

    private:
        static constexpr float mMinPeriod = 1000.0f; // In ms

        Sensor::Driver    mDriver;
        Sensor::SourceTick mTick;

        uint8_t  mArbBpm    { 0 };
        uint8_t  mArbTrust  { 0 };
        HrSource mSource    { HrSource::None };
        uint8_t  mOptBpm    { 0 };
        uint8_t  mOptTrust  { 0 };
        uint8_t  mExtBpm    { 0 };
        uint8_t  mExtTrust  { 0 };
        OS::Mutex mDataMutex;
    };

} // namespace Sensor

#endif /* __BODY_SUB_SENSOR_HEART_RATE_EX_HPP */
