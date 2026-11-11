/**
 ******************************************************************************
 * @file    MagEllipsoidFit.hpp
 * @brief   Ellipsoid fit for magnetometer soft-iron calibration.
 *
 *          Fits a centre and a symmetric positive-definite shape matrix with
 *          axes free to point anywhere. The correction is that matrix's
 *          principal square root (symmetric), scaled to the ellipsoid's mean
 *          radius. Moments accumulate shifted by the first sample and divided
 *          by a fixed scale, so orders one to four stay of similar size.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_ELLIPSOID_FIT_HPP
#define COMPONENT_MAG_ELLIPSOID_FIT_HPP

#include <cstdint>

class MagEllipsoidFit
{

public:

    struct Result {
        float offsetX = 0.0f;   ///< Centre, i.e. the hard-iron offset.
        float offsetY = 0.0f;
        float offsetZ = 0.0f;

        /// Correction matrix, upper triangle: maps the ellipsoid, offset
        /// removed, onto a sphere of `radius`.
        float m00 = 1.0f;
        float m01 = 0.0f;
        float m02 = 0.0f;
        float m11 = 1.0f;
        float m12 = 0.0f;
        float m22 = 1.0f;

        /// Geometric mean of the semi-axes.
        float radius = 0.0f;

        /// Semi-axes, longest first.
        float semiAxis[3] = { 0.0f, 0.0f, 0.0f };
    };

    MagEllipsoidFit();

    void reset();

    void add(float x, float y, float z);

    uint32_t getCount() const;

    /**
     * @brief   Solve for the best-fitting ellipsoid. No residual is reported.
     * @retval  'false', result untouched, on too few samples, singular normal
     *          equations, or a quadric that is not an ellipsoid (a tumble too
     *          narrow to pin the shape).
     */
    bool solve(Result& result) const;

    /// Nine unknowns: the floor for the arithmetic, not for a usable fit.
    static constexpr uint32_t skMinSamples = 20;

private:

    /// Cyclic Jacobi eigen-decomposition of a symmetric 3x3; unlike the
    /// closed form it stays stable when two eigenvalues coincide.
    static void jacobiEigen(const double a[3][3], double vectors[3][3],
                            double values[3]);

    /// Augmented 9x10 normal equations. A member, not a stack local, because
    /// solve() runs on a thread this class does not size. Per-call scratch,
    /// unguarded: one fit is one object, never shared.
    mutable double mAugmented[9][10];

    uint32_t mCount;

    float mRefX;
    float mRefY;
    float mRefZ;

    // Fourth order.
    double mX4, mY4, mZ4;
    double mX2Y2, mX2Z2, mY2Z2;
    double mX3Y, mX3Z, mXY3, mY3Z, mXZ3, mYZ3;
    double mX2YZ, mXY2Z, mXYZ2;

    // Third order.
    double mX3, mY3, mZ3;
    double mX2Y, mX2Z, mXY2, mY2Z, mXZ2, mYZ2, mXYZ;

    // Second order.
    double mX2, mY2, mZ2, mXY, mXZ, mYZ;

    // First order.
    double mX, mY, mZ;

    /// Brings field values to about one; cancels when the result is scaled
    /// back.
    static constexpr double skNormScale = 100.0;

};

#endif // COMPONENT_MAG_ELLIPSOID_FIT_HPP
