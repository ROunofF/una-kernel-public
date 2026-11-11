/**
 ******************************************************************************
 * @file    InstanceMagCal.hpp
 * @brief   The watch's magnetometer calibration.
 *
 *          The correction in force and where it is kept; the store and the
 *          axis frame are board facts answered only here.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INSTANCE_MAG_CAL_HPP
#define INSTANCE_MAG_CAL_HPP

#include "IMagCorrection.hpp"
#include "Magnetometer/MagCalibration.hpp"

namespace Instance {

class MagCal
{

public:

    /**
     * @brief   The correction to hand to whoever publishes magnetic samples.
     * @note    Built on first use from what was kept. Takes its own lock, so
     *          it is safe on a publishing thread while being replaced.
     */
    static Interface::IMagCorrection& correction();

    /**
     * @brief   Apply a correction and keep it across a restart.
     * @retval  'true' if applied and kept. 'false' after an accepted record
     *          means a restart comes up without it.
     */
    static bool store(const MagCalibration::Record& record);

    /// 'true', with record filled in, if a correction is being applied.
    static bool read(MagCalibration::Record& record);

    /**
     * @brief   Stop correcting and erase the kept correction.
     * @retval  'false' if the erase failed: a restart brings it back.
     */
    static bool clear();

};

} // namespace Instance

#endif // INSTANCE_MAG_CAL_HPP
