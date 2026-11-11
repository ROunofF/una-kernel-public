/**
 ******************************************************************************
 * @file    InstanceMagCalRunner.hpp
 * @brief   The watch's magnetometer calibration run.
 *
 *          At most one run at a time, on a thread of its own, freed once its
 *          owner has the result. A finished run does not put its result in
 *          force.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INSTANCE_MAG_CAL_RUNNER_HPP
#define INSTANCE_MAG_CAL_RUNNER_HPP

#include <cstdint>

#include "Magnetometer/MagCalibrationSession.hpp"

namespace Instance {

class MagCalRunner
{

public:

    using Mode   = MagCalibrationSession::Mode;
    using Result = MagCalibrationSession::Result;

    /// What the session has taken in, spans zero in a check, and how far the
    /// run is along.
    struct Progress : MagCalibrator::Progress {
        uint8_t percent = 0;    ///< Decided by the run; never decreases.
    };

    /**
     * @brief   Whoever started a run: where it reports, and who may end it.
     * @note    No call here may wait on anything unbounded.
     */
    class Owner
    {
    public:
        /// On the run's thread, every half second, never after onMagCalDone().
        virtual void onMagCalProgress(const Progress& progress)
        {
            (void) progress;
        }

        /// On the sensor layer's delivery thread, every sample, as measured.
        virtual void onMagCalSample(uint64_t timestampUs,
                                    float x, float y, float z)
        {
            (void) timestampUs;
            (void) x;
            (void) y;
            (void) z;
        }

        /// On the run's thread, exactly once for every start() that returned
        /// 'true'. A new run cannot be started from inside this call.
        virtual void onMagCalDone(const Result& result) = 0;

    protected:
        virtual ~Owner() = default;
    };

    /**
     * @brief   Start a run of at most maxSeconds, capped at skMaxSeconds.
     * @param   owner: Must outlive the run.
     * @param   mode: FIT, or CHECK of the correction in force.
     * @retval  'false', with no callback to follow, if a run exists,
     *          maxSeconds is zero, a check has no correction to check, or the
     *          run could not be created.
     */
    static bool start(Owner& owner, Mode mode, uint32_t maxSeconds);

    /**
     * @brief   Ask the run this owner started to end early.
     * @retval  'true' if taken, and onMagCalDone() follows; 'false' if this
     *          owner has no run, or its run is already ending on its own, in
     *          which case onMagCalDone() still follows.
     */
    static bool cancel(const Owner& owner);

    static constexpr uint32_t skMaxSeconds = 600;

};

} // namespace Instance

#endif // INSTANCE_MAG_CAL_RUNNER_HPP
