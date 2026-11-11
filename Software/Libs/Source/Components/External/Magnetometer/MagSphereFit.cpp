/**
 ******************************************************************************
 * @file    MagSphereFit.cpp
 * @brief   Least-squares sphere fit for magnetometer hard-iron calibration.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagSphereFit.hpp"
#include "MagLinearSolve.hpp"

#include <cmath>

MagSphereFit::MagSphereFit()
{
    reset();
}

void MagSphereFit::reset()
{
    mCount = 0;

    mSx = mSy = mSz = 0.0;
    mSxx = mSyy = mSzz = 0.0;
    mSxy = mSxz = mSyz = 0.0;
    mSs = mSsx = mSsy = mSsz = mSss = 0.0;

    mMinX = mMinY = mMinZ = 0.0f;
    mMaxX = mMaxY = mMaxZ = 0.0f;
}

void MagSphereFit::add(float x, float y, float z)
{
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double dz = static_cast<double>(z);
    const double s  = dx * dx + dy * dy + dz * dz;

    mSx  += dx;
    mSy  += dy;
    mSz  += dz;

    mSxx += dx * dx;
    mSyy += dy * dy;
    mSzz += dz * dz;
    mSxy += dx * dy;
    mSxz += dx * dz;
    mSyz += dy * dz;

    mSs  += s;
    mSsx += s * dx;
    mSsy += s * dy;
    mSsz += s * dz;
    mSss += s * s;

    if (mCount == 0) {
        mMinX = mMaxX = x;
        mMinY = mMaxY = y;
        mMinZ = mMaxZ = z;
    } else {
        if (x < mMinX) { mMinX = x; }
        if (x > mMaxX) { mMaxX = x; }
        if (y < mMinY) { mMinY = y; }
        if (y > mMaxY) { mMaxY = y; }
        if (z < mMinZ) { mMinZ = z; }
        if (z > mMaxZ) { mMaxZ = z; }
    }

    mCount++;
}

uint32_t MagSphereFit::getCount() const
{
    return mCount;
}

float MagSphereFit::getSpanX() const
{
    return (mCount == 0) ? 0.0f : (mMaxX - mMinX);
}

float MagSphereFit::getSpanY() const
{
    return (mCount == 0) ? 0.0f : (mMaxY - mMinY);
}

float MagSphereFit::getSpanZ() const
{
    return (mCount == 0) ? 0.0f : (mMaxZ - mMinZ);
}

bool MagSphereFit::solve(Result& result) const
{
    if (mCount < skMinSamples) {
        return false;
    }

    const double n  = static_cast<double>(mCount);
    const double mx = mSx / n;
    const double my = mSy / n;
    const double mz = mSz / n;

    // Second moments about the mean. Uncentred, the normal equations trade
    // centre against radius almost freely.
    double m[3][3];
    m[0][0] = mSxx - n * mx * mx;
    m[1][1] = mSyy - n * my * my;
    m[2][2] = mSzz - n * mz * mz;
    m[0][1] = m[1][0] = mSxy - n * mx * my;
    m[0][2] = m[2][0] = mSxz - n * mx * mz;
    m[1][2] = m[2][1] = mSyz - n * my * mz;

    const double q = mx * mx + my * my + mz * mz;

    // sum of s' where s' = |p - mean|^2
    const double sumS = mSs - 2.0 * (mx * mSx + my * mSy + mz * mSz) + n * q;

    // sum of s' * p', component by component.
    const double sPx = mx * mSxx + my * mSxy + mz * mSxz;   // sum of (p . mean) * x
    const double sPy = mx * mSxy + my * mSyy + mz * mSyz;
    const double sPz = mx * mSxz + my * mSyz + mz * mSzz;

    double v[3];
    v[0] = mSsx - 2.0 * sPx - mx * mSs + 2.0 * n * q * mx;
    v[1] = mSsy - 2.0 * sPy - my * mSs + 2.0 * n * q * my;
    v[2] = mSsz - 2.0 * sPz - mz * mSs + 2.0 * n * q * mz;

    // Centred normal equations: 2 * M * c' = v, with k' = sumS / n.
    double rhs[3] = { v[0] * 0.5, v[1] * 0.5, v[2] * 0.5 };

    double system[3][4];
    for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            system[row][col] = m[row][col];
        }
        system[row][3] = rhs[row];
    }

    double c[3];
    if (!MagLinearSolve::solve<3>(system, c)) {
        return false;
    }

    const double k = sumS / n;
    const double r2 = k + c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    if (!(r2 > 0.0)) {
        return false;
    }

    const double radius = std::sqrt(r2);

    // Algebraic residual, in squared-field units; divided by twice the radius
    // below to give distance from the surface.
    const double cv    = c[0] * v[0] + c[1] * v[1] + c[2] * v[2];
    const double sumS2 = mSss
                       + 4.0 * (mx * sPx + my * sPy + mz * sPz)
                       + n * q * q
                       - 4.0 * (mx * mSsx + my * mSsy + mz * mSsz)
                       + 2.0 * q * mSs
                       - 4.0 * q * n * q;
    double sumErr2 = sumS2 - 2.0 * cv - (sumS * sumS) / n;
    if (sumErr2 < 0.0) {
        // Rounding in the cancellation of large moments.
        sumErr2 = 0.0;
    }

    result.offsetX     = static_cast<float>(mx + c[0]);
    result.offsetY     = static_cast<float>(my + c[1]);
    result.offsetZ     = static_cast<float>(mz + c[2]);
    result.radius      = static_cast<float>(radius);
    result.residualRms = static_cast<float>(std::sqrt(sumErr2 / n) / (2.0 * radius));

    return true;
}
