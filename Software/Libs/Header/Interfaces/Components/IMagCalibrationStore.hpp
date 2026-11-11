/**
 ******************************************************************************
 * @file    IMagCalibrationStore.hpp
 * @brief   Where a magnetometer calibration is kept between runs.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_IMAG_CALIBRATION_STORE_HPP
#define INTERFACE_IMAG_CALIBRATION_STORE_HPP

#include "Magnetometer/MagCalibration.hpp"

namespace Interface
{

class IMagCalibrationStore
{

public:

    /**
     * @brief   Write a calibration; the fit statistics need not survive.
     * @retval  'true' if it was stored and can be read back.
     */
    virtual bool save(const MagCalibration::Record& record) = 0;

    /**
     * @brief   Read the stored calibration. Failing is ordinary. The caller
     *          still puts the record through MagCalibration::set(), which
     *          rejects intact but unusable values.
     * @retval  'true' if an intact record was read; record untouched otherwise.
     */
    virtual bool load(MagCalibration::Record& record) const = 0;

    /**
     * @brief   Erase the stored calibration, so the next load() fails.
     * @retval  'true' if the erase is known to have landed.
     */
    virtual bool clear() = 0;

protected:

    virtual ~IMagCalibrationStore() = default;
};

} // namespace Interface

#endif // INTERFACE_IMAG_CALIBRATION_STORE_HPP
