/**
 ******************************************************************************
 * @file    MagSubSensorField.hpp
 * @brief   SubSensor for the magnetic field.
 *
 *          Publishes the corrected field on SDK::Sensor::Type::MAGNETIC_FIELD.
 *          Owns no hardware: it subscribes to the raw channel, and each sample
 *          delivered is corrected and published synchronously, so nothing is
 *          buffered here.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef MAG_SUB_SENSOR_FIELD_HPP
#define MAG_SUB_SENSOR_FIELD_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "IMagCorrection.hpp"

#include <atomic>
#include <cstdint>

namespace Sensor
{
    class MagSubSensorField : public Sensor::ISubSensor,
                              public Interface::ISensor,
                              public SDK::Interface::ISensorDataListener
    {
    public:
        /**
         * @brief   Construct the corrected channel on top of the raw one.
         * @param   driverFieldRaw: The field as measured. Subscribed to while
         *          a consumer is connected here, which is what brings the part
         *          up.
         * @param   ctrl: The binding, for the control plane.
         */
        MagSubSensorField(Sensor::Driver&            driverFieldRaw,
                          Sensor::ISensorDriverCtrl& ctrl);

        /**
         * @brief   The correction to apply to every sample.
         * @note    Set before any consumer can connect and not changed
         *          afterwards.
         * @param   pCalibrator: Not owned. Null leaves samples as measured,
         *          reported as uncalibrated.
         */
        void setCalibrator(const Interface::IMagCorrection* pCalibrator);

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

        //// ISensorDataListener
        void onSdlNewData(uint16_t                 handle,
                          const SDK::Sensor::Data* base,
                          uint16_t                 count,
                          uint16_t                 stride) override;

        /// The fastest a consumer may ask this channel to be read, in ms.
        /// Consumers that want every measurement read the raw channel.
        static constexpr uint32_t skMinPeriod = 10;

    private:
        /// Whether a consumer is connected, and the period it asked for. The
        /// period is applied by the upstream subscription, not here.
        bool  mActive   = false;
        float mPeriodMs = 0.0f;
        /// mPeriodMs in us for the raw channel's delivery thread, which reads
        /// it with the raw step to know the step of its own; relaxed.
        std::atomic<uint32_t> mPeriodUs{0};

        Sensor::Driver& mDriverFieldRaw;

        Sensor::Driver  mDriver;

        /// Consulted once per sample, on the raw channel's delivery thread.
        /// Null until the part is up.
        const Interface::IMagCorrection* mpCalibrator = nullptr;
    };

} // namespace Sensor

#endif // MAG_SUB_SENSOR_FIELD_HPP
