/**
 ******************************************************************************
 * @file    BaroP0Adjuster.hpp
 * @brief   Sea level pressure (P0) reference calculator.
 *
 *          Averages a short unbroken run of pressure samples and derives the
 *          sea-level reference P0 for a known altitude with the barometric
 *          formula. Arithmetic only: no clock, sensor or thread. The owner
 *          feeds it samples, keeps them coming while it is active, and takes
 *          the result when it completes.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_BARO_P0_ADJUSTER_HPP
#define SENSOR_BARO_P0_ADJUSTER_HPP

#include <cstdint>

namespace Sensor
{
    class BaroP0Adjuster
    {
    public:

        /**
         * @brief   Start, or start over, averaging for a known altitude.
         * @param   altitude: Altitude above sea level, metres.
         * @param   nowMs:    The millisecond clock the samples are stamped on.
         * @retval  'false', with nothing started and a run in progress left
         *          as it was, for an altitude that is not a number or is
         *          outside skMinAltitudeM..skMaxAltitudeM.
         */
        bool start(float altitude, uint32_t nowMs);

        /**
         * @brief   Abandon a run that has not completed in time.
         * @param   nowMs: The millisecond clock.
         * @note    A run ends on its last sample, so with no samples coming
         *          nothing would end it, and its owner would go on asking
         *          for them. To be called wherever the owner is about to
         *          act on isActive(); update() does it for itself.
         * @retval  'true' as a run is abandoned.
         */
        bool expire(uint32_t nowMs);

        /**
         * @brief   Whether samples are still being gathered.
         * @note    True from start() until update() has taken the last sample
         *          it needs, whether or not the P0 that came out was valid,
         *          or until the run is abandoned. While true the owner keeps
         *          the samples coming.
         */
        bool isActive() const { return mActive; }

        /**
         * @brief   Feed a pressure sample into the average.
         * @param   pressure: Measured pressure, mbar.
         * @param   nowMs:    When it was measured, ms.
         * @note    A sample that comes later than skMaxGapMs after the one
         *          before starts the run over from itself: the stream stalled
         *          and resumed, and averaging across the gap would mix
         *          pressures that do not belong together. The altitude is
         *          kept.
         * @retval  'true' once, as the run completes with a valid P0, to be
         *          read with getP0(); 'false' while gathering, when the run
         *          is abandoned, or if no finite P0 came out.
         */
        bool update(float pressure, uint32_t nowMs);

        /**
         * @brief   The sea-level pressure of the samples gathered so far, Pa.
         * @note    Always finite: the standard atmosphere before any sample,
         *          and when the samples give no finite P0.
         *          Exact only once update() has returned 'true'.
         */
        float getP0() const;

        /// Samples averaged for one P0.
        static constexpr uint32_t skSamples  = 10;

        /// The longest step between two samples of one run.
        static constexpr uint32_t skMaxGapMs = 2000;

        /// The longest a run may take from its start. One takes about a
        /// second at the rate it is run at; the rest is room for a part
        /// that had to be brought up or power cycled once on the way.
        static constexpr uint32_t skDeadlineMs = 5000;

        /// The altitudes a run is started for, metres: below the lowest
        /// dry land, and above the highest ground.
        static constexpr float    skMinAltitudeM = -500.0f;
        static constexpr float    skMaxAltitudeM = 9000.0f;

        /// Standard atmosphere, Pa.
        static constexpr float    skDefaultP0 = 101325.0f;

    private:

        /// Sea-level pressure, Pa, from a pressure in Pa measured at an
        /// altitude in metres; NaN where the formula gives no finite one.
        static float computeP0FromAltitude(float p, float altitude);

        /// P0 from the samples gathered so far; NaN with none, or where
        /// the formula gives no finite one.
        float computeCurrentP0() const;

        float    mAltitude      = 0.0f;
        bool     mActive        = false;
        float    mPressureSumma = 0.0f;
        uint32_t mCount         = 0;
        uint32_t mLastSampleMs  = 0;
        uint32_t mStartedMs     = 0;
    };

} // namespace Sensor

#endif // SENSOR_BARO_P0_ADJUSTER_HPP
