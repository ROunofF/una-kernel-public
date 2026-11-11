/**
 ******************************************************************************
 * @file    BodySubSensorHeartRate.hpp
 * @brief   SubSensor for the Heart Rate
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __BODY_SUB_SENSOR_HEART_RATE_HPP
#define __BODY_SUB_SENSOR_HEART_RATE_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Sensors/Body/HrSourceArbiter.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class BodySubSensorHeartRate : public Sensor::ISubSensor,
                                   public Interface::ISensor
    {
    public:
        BodySubSensorHeartRate(Sensor::ISensorDriverCtrl& ctrl);

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

        // Set the (already wrist-gated) arbitrated reading for the 2-field
        // HEART_RATE frame. Gating + source live in SensorBody::publishHr.
        void setData(uint8_t hr, uint8_t trustLevel);

    private:
        static constexpr float    mMinPeriod = 1000.0f; // In ms

        Sensor::Driver     mDriver;
        Sensor::SourceTick mTick;
        uint8_t            mHr;
        uint8_t            mTrustLevel;
        OS::Mutex          mDataMutex;
    };

} // namespace Sensor

#endif /* __BODY_SUB_SENSOR_HEART_RATE_HPP */
