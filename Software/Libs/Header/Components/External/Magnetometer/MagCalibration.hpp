/**
 ******************************************************************************
 * @file    MagCalibration.hpp
 * @brief   Magnetometer calibration record and the policy that accepts one.
 *
 *          A correction subtracts a hard-iron offset, then applies a soft-iron
 *          matrix: the principal square root of the ellipsoid's positive-
 *          definite shape matrix, which is symmetric, so six elements.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_CALIBRATION_HPP
#define COMPONENT_MAG_CALIBRATION_HPP

#include <cstdint>

class MagCalibration
{

public:

    /**
     * @brief   What a calibrated device carries, in the samples' unit.
     * @note    A plain aggregate, so it can be persisted as it stands.
     */
    struct Record {
        float offsetX = 0.0f;   ///< Hard-iron offset, subtracted from a sample.
        float offsetY = 0.0f;
        float offsetZ = 0.0f;

        /// Soft-iron matrix, upper triangle; the identity when uninitialised.
        float m00 = 1.0f;
        float m01 = 0.0f;
        float m02 = 0.0f;
        float m11 = 1.0f;
        float m12 = 0.0f;
        float m22 = 1.0f;

        /// Equal-volume radius of the measured ellipsoid, which the correction
        /// maps onto, so corrected samples come out at this magnitude. It is
        /// the field's magnitude only without soft iron; with it, that times
        /// the cube root of the soft-iron matrix's determinant.
        float radius   = 0.0f;

        /// RMS distance of the raw samples from the best-fitting sphere: the
        /// size of the distortion, not the error left after correcting it.
        float residual = 0.0f;

        uint32_t samples = 0;   ///< Samples the fit was built from.
    };

    /**
     * @brief   Thresholds a fit has to clear.
     */
    struct Limits {
        uint32_t minSamples = 300;

        /// How many of the 26 cube directions around the field's centre the
        /// tumble must reach; independent of how the watch was held.
        uint8_t minCoveredDirections = 20;

        /// Max RMS distance of the raw samples from a sphere, as a fraction of
        /// the radius. A sanity bound: the ellipsoid removes this anisotropy.
        float maxResidualFactor = 0.25f;

        /// Max RMS scatter of the CORRECTED magnitude on samples the fit never
        /// saw, as a fraction of the radius. Provisional.
        float maxCorrectedSpreadFactor = 0.25f;

        /// Plausible radius: a field magnitude, as scaled by soft iron.
        float minRadius = 15.0f;
        float maxRadius = 90.0f;
    };

    enum class Verdict : uint8_t {
        OK = 0,                 ///< Usable.
        TOO_FEW_SAMPLES,        ///< The sweep was too short.
        COVERAGE_TOO_NARROW,    ///< Too little of the sphere was covered.
        RESIDUAL_TOO_HIGH,      ///< The samples do not lie on a sphere.
        RADIUS_OUT_OF_RANGE,    ///< The fitted radius is not a plausible one.
        NOT_USABLE,             ///< Not a record set() would take.
    };

    MagCalibration();

    /**
     * @brief   Judge a fit against the limits. Pure.
     * @retval  Verdict::OK, or the first thing found wrong.
     */
    static Verdict evaluate(const Record& record, uint8_t coveredDirections,
                            const Limits& limits);

    static const char* describe(Verdict verdict);

    /**
     * @brief   Adopt a record; a rejected one leaves the current one in force.
     * @retval  'true' if adopted.
     */
    bool set(const Record& record);

    void clear();

    bool isValid() const;

    const Record& get() const;

    /**
     * @brief   Correct one sample in place; a no-op while none is held.
     * @retval  Whether a calibration was held and applied.
     */
    bool apply(float& x, float& y, float& z) const;

private:

    /// Finite values, a positive radius, a residual that is not negative and a
    /// positive-definite matrix. It rejects a corrupt record read back from a
    /// store, and evaluate() applies it too, so an accepted fit is always one
    /// set() takes.
    static bool isUsable(const Record& record);

    Record mRecord;
    bool   mValid;

};

#endif // COMPONENT_MAG_CALIBRATION_HPP
