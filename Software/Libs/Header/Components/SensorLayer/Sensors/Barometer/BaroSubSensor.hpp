/**
 ******************************************************************************
 * @file    BaroSubSensor.hpp
 * @brief   SubSensor for one channel of the barometer.
 *
 *          Publishes the pressure on SDK::Sensor::Type::PRESSURE or the
 *          temperature on SDK::Sensor::Type::AMBIENT_TEMPERATURE, as the part
 *          measured them.
 *
 *          Measurements are queued rather than read live: they arrive on the
 *          part's own clock, not the sweep's. The sweep is woken for each and
 *          drains the queue, and each consumer's own queue thins that to its
 *          period, judged on the instant the measurement was started.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_BARO_SUB_SENSOR_HPP
#define SENSOR_BARO_SUB_SENSOR_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SourceStepTrack.hpp"
#include "OS.hpp"

#include <cstddef>
#include <cstdint>

namespace Sensor
{
    class BaroSubSensor : public Sensor::ISubSensor,
                          public Interface::ISensor
    {
    public:

        enum class Channel : uint8_t {
            PRESSURE = 0,
            TEMPERATURE,
        };

        /// One measurement of this channel with the instant it was started.
        struct Sample {
            uint64_t timestampUs = 0;

            /// Pressure in mbar, or temperature in degrees Celsius.
            float    value       = 0.0f;

            /// Pressure only: the sea-level reference in force when the
            /// measurement was taken, Pa.
            float    seaLevelPa  = 0.0f;
        };

        class DataProvider {
        public:
            /**
             * @brief   Ask the part for this period on behalf of a channel,
             *          or for none.
             * @param   channel: The channel asking.
             * @param   value: Period in ms, or the maximum to release the
             *          part.
             * @retval  'true' if the part accepted the request.
             */
            virtual bool requestPeriod(Channel channel, uint32_t value) = 0;

            /**
             * @brief   The shortest period the part can satisfy, ms.
             */
            virtual float getMinPeriod() = 0;

        protected:

            virtual ~DataProvider() = default;
        };

        BaroSubSensor(Channel channel, DataProvider& provider,
                      Sensor::ISensorDriverCtrl& ctrl);

        /**
         * @brief   Create the buffer between the part and the sweep.
         * @note    Nothing may be published until it has succeeded.
         */
        bool init();

        /**
         * @brief   Take a measurement from the part.
         * @note    Called on whichever thread the part delivers on, and it
         *          never waits.
         * @param   sample:   The measurement.
         * @param   periodUs: The period the part measured it at, reported to
         *          the listeners as the source step.
         */
        void addNewData(const Sample& sample, uint32_t periodUs);

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

    private:

        /// How far the part may run ahead of the sweep before a measurement
        /// is lost: more than a second at the fastest rung.
        static constexpr size_t skQueueDepth = 16;

        const Channel mChannel;

        /// Whether a consumer is connected, and the period it asked for. The
        /// period is passed down to the part, not applied here: thinning is
        /// done per consumer, downstream.
        bool  mActive   = false;
        float mPeriodMs = 0.0f;

        /// Set by the part's thread when a measurement had to displace one,
        /// cleared and reported by the sweep.
        volatile bool mDropped = false;

        /// Whether the loss above has already been said, so an episode is
        /// reported once. Touched only by the sweep.
        bool mDropReported = false;

        DataProvider&  mProvider;
        Sensor::Driver mDriver;

        /// Filled by the part's thread, drained by the sweep. The producer
        /// never waits; when full, it drops the oldest.
        OS::Queue<Sample, skQueueDepth> mQueue;

        /// The step each measurement held in mQueue was taken at.
        Sensor::SourceStepTrack mSteps;
    };

} // namespace Sensor

#endif // SENSOR_BARO_SUB_SENSOR_HPP
