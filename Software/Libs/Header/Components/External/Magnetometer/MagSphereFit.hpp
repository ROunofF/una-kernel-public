/**
 ******************************************************************************
 * @file    MagSphereFit.hpp
 * @brief   Least-squares sphere fit for magnetometer hard-iron calibration.
 *
 *          The centre is the hard-iron offset and the radius the field
 *          magnitude. Running moments keep memory fixed; the solve is
 *          mean-centred for conditioning. Soft iron is not modelled and shows
 *          only as a raised residual.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_SPHERE_FIT_HPP
#define COMPONENT_MAG_SPHERE_FIT_HPP

#include <cstdint>

class MagSphereFit
{

public:

    struct Result {
        float offsetX     = 0.0f;   ///< Sphere centre, i.e. hard-iron offset.
        float offsetY     = 0.0f;
        float offsetZ     = 0.0f;
        float radius      = 0.0f;   ///< Sphere radius; the field magnitude only without soft iron.
        float residualRms = 0.0f;   ///< RMS distance of samples from the sphere.
    };

    MagSphereFit();

    void reset();

    void add(float x, float y, float z);

    uint32_t getCount() const;

    /// Extent along one axis. Well under twice the radius means the rotation
    /// was too narrow to separate centre from radius, whatever the residual.
    float getSpanX() const;
    float getSpanY() const;
    float getSpanZ() const;

    /**
     * @brief   Solve for the best-fitting sphere.
     * @retval  'false', result untouched, on too few samples or singular
     *          normal equations (identical, collinear or coplanar samples).
     */
    bool solve(Result& result) const;

    /// Four unknowns; solve() refuses below twice that.
    static constexpr uint32_t skMinSamples = 8;

private:

    uint32_t mCount;

    // First and second moments.
    double mSx, mSy, mSz;
    double mSxx, mSyy, mSzz, mSxy, mSxz, mSyz;

    // Moments involving s = x^2 + y^2 + z^2, the sphere fit's target.
    double mSs, mSsx, mSsy, mSsz, mSss;

    float mMinX, mMaxX;
    float mMinY, mMaxY;
    float mMinZ, mMaxZ;

};

#endif // COMPONENT_MAG_SPHERE_FIT_HPP
