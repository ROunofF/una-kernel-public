/**
 ******************************************************************************
 * @file    MagSubSensorFieldRaw.hpp
 * @brief   SubSensor for the uncorrected magnetic field.
 *
 *          Publishes the field as measured on
 *          SDK::Sensor::Type::MAGNETIC_FIELD_RAW. This is the source: the
 *          corrected channel subscribes here and corrects what it gets.
 *
 *          Measurements are queued rather than read live: the part has no
 *          FIFO and can outrun the sensor layer's sweep. The sweep drains the
 *          queue, and each consumer's own queue thins that to its period,
 *          judged on the instant the field was measured.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef MAG_SUB_SENSOR_FIELD_RAW_HPP
#define MAG_SUB_SENSOR_FIELD_RAW_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SourceStepTrack.hpp"
#include "OS.hpp"

#include <cstddef>
#include <cstdint>

namespace Sensor
{
    class MagSubSensorFieldRaw : public Sensor::ISubSensor,
                                 public Interface::ISensor
    {
    public:
        /// One measurement with the instant it was taken; consumers are paced
        /// on that instant, not on when the sweep delivered it.
        struct Sample {
            uint64_t timestampUs = 0;
            float    x           = 0.0f;
            float    y           = 0.0f;
            float    z           = 0.0f;
        };

        class DataProvider {
        public:
            /**
             * @brief   Ask the part for this period, or for none.
             * @note    Called only from start(), stop() and updatePeriod(),
             *          which this sub-sensor's driver serialises; an
             *          implementation needs no lock of its own.
             * @param   value: Period in ms, or the maximum to release the part.
             * @retval  'true' if the part accepted the request.
             */
            virtual bool requestRawFieldPeriod(uint32_t value) = 0;

            /**
             * @brief   The shortest period the part can actually satisfy, ms.
             */
            virtual float getMinRawFieldPeriod() = 0;

        protected:
            virtual ~DataProvider() = default;
        };

        MagSubSensorFieldRaw(DataProvider& provider,
                             Sensor::ISensorDriverCtrl& ctrl);

        /**
         * @brief   Create the buffer between the part and the sweep.
         * @note    Nothing may be published until it has succeeded.
         */
        bool init();

        /**
         * @brief   Take measurements from the part.
         * @note    Called on whichever thread the part delivers on, and it
         *          never waits.
         * @param   data:     The measurements, oldest first.
         * @param   count:    How many.
         * @param   periodUs: The period the part converted them at, reported
         *          to the listeners as the source step.
         */
        void addNewData(const Sample* data, size_t count, uint32_t periodUs);

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
        /// How far the part may run ahead of the sweep before a measurement is
        /// lost. Chosen with room rather than derived: the sweep is shared
        /// with every other sensor and its delays are not predictable.
        static constexpr size_t skQueueDepth = 200;

        /// Whether a consumer is connected, and the period it asked for. The
        /// period is passed down to the part, not applied here: thinning is
        /// done per consumer, downstream.
        bool  mActive   = false;
        float mPeriodMs = 0.0f;

        /// Set by the part's thread when a measurement had to displace one,
        /// cleared and reported by the sweep, which has the stack for logging.
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

#endif // MAG_SUB_SENSOR_FIELD_RAW_HPP
