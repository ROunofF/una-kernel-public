/**
 ******************************************************************************
 * @file    IMagCorrection.hpp
 * @brief   Something that corrects a magnetic field sample.
 *
 *          Separate from the magnetometer's interface: the part reports what
 *          it measured; correction is applied where the sample is published.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_IMAG_CORRECTION_HPP
#define INTERFACE_IMAG_CORRECTION_HPP

namespace Interface
{

/**
 * @brief   Something that corrects a magnetic field sample.
 * @note    Not owned; consulted once per sample on the publishing thread.
 */
class IMagCorrection
{

public:

    /**
     * @brief   Correct one sample, in uT, in place.
     * @retval  Whether a correction was applied, answered by the same look
     *          that applied it; a separate isCalibrated() call can race.
     */
    virtual bool apply(float& x, float& y, float& z) const = 0;

    /// Whether a correction is held. None is not an error: samples pass as is.
    virtual bool isCalibrated() const = 0;

protected:

    virtual ~IMagCorrection() = default;
};

} // namespace Interface

#endif // INTERFACE_IMAG_CORRECTION_HPP
