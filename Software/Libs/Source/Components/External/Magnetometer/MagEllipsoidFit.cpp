/**
 ******************************************************************************
 * @file    MagEllipsoidFit.cpp
 * @brief   Ellipsoid fit for magnetometer soft-iron calibration.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagEllipsoidFit.hpp"
#include "MagLinearSolve.hpp"

#include <cmath>

MagEllipsoidFit::MagEllipsoidFit()
{
    reset();
}

void MagEllipsoidFit::reset()
{
    mCount = 0;

    mRefX = mRefY = mRefZ = 0.0f;

    mX4 = mY4 = mZ4 = 0.0;
    mX2Y2 = mX2Z2 = mY2Z2 = 0.0;
    mX3Y = mX3Z = mXY3 = mY3Z = mXZ3 = mYZ3 = 0.0;
    mX2YZ = mXY2Z = mXYZ2 = 0.0;

    mX3 = mY3 = mZ3 = 0.0;
    mX2Y = mX2Z = mXY2 = mY2Z = mXZ2 = mYZ2 = mXYZ = 0.0;

    mX2 = mY2 = mZ2 = mXY = mXZ = mYZ = 0.0;

    mX = mY = mZ = 0.0;
}

void MagEllipsoidFit::add(float x, float y, float z)
{
    if (mCount == 0) {
        // The first sample is the origin, so the shift suits any field.
        mRefX = x;
        mRefY = y;
        mRefZ = z;
    }

    const double dx = (static_cast<double>(x) - mRefX) / skNormScale;
    const double dy = (static_cast<double>(y) - mRefY) / skNormScale;
    const double dz = (static_cast<double>(z) - mRefZ) / skNormScale;

    const double xx = dx * dx;
    const double yy = dy * dy;
    const double zz = dz * dz;
    const double xy = dx * dy;
    const double xz = dx * dz;
    const double yz = dy * dz;

    mX4 += xx * xx;
    mY4 += yy * yy;
    mZ4 += zz * zz;

    mX2Y2 += xx * yy;
    mX2Z2 += xx * zz;
    mY2Z2 += yy * zz;

    mX3Y += xx * xy;
    mX3Z += xx * xz;
    mXY3 += yy * xy;
    mY3Z += yy * yz;
    mXZ3 += zz * xz;
    mYZ3 += zz * yz;

    mX2YZ += xx * yz;
    mXY2Z += yy * xz;
    mXYZ2 += zz * xy;

    mX3 += xx * dx;
    mY3 += yy * dy;
    mZ3 += zz * dz;

    mX2Y += xx * dy;
    mX2Z += xx * dz;
    mXY2 += dx * yy;
    mY2Z += yy * dz;
    mXZ2 += dx * zz;
    mYZ2 += dy * zz;
    mXYZ += xy * dz;

    mX2 += xx;
    mY2 += yy;
    mZ2 += zz;
    mXY += xy;
    mXZ += xz;
    mYZ += yz;

    mX += dx;
    mY += dy;
    mZ += dz;

    mCount++;
}

uint32_t MagEllipsoidFit::getCount() const
{
    return mCount;
}

bool MagEllipsoidFit::solve(Result& result) const
{
    if (mCount < skMinSamples) {
        return false;
    }

    // Fitting the quadric x'A x + b'x + c = 0, A symmetric, normalised by
    // tr(A) = 3. Any ellipsoid can be scaled to that, and unlike fixing c it
    // works with the origin on the surface, which the first sample is.
    // Substituting a22 = 3 - a00 - a11 leaves ordinary least squares over
    //     u = [a00, a11, 2a01, 2a02, 2a12, b0, b1, b2, c]
    //     row = [x^2 - z^2, y^2 - z^2, xy, xz, yz, x, y, z, 1]
    //     target = -3 z^2
    const double n = static_cast<double>(mCount);

    const double d11 = mX4 - 2.0 * mX2Z2 + mZ4;
    const double d12 = mX2Y2 - mX2Z2 - mY2Z2 + mZ4;
    const double d22 = mY4 - 2.0 * mY2Z2 + mZ4;

    double (&m)[9][10] = mAugmented;

    m[0][0] = d11;
    m[0][1] = d12;
    m[0][2] = mX3Y - mXYZ2;
    m[0][3] = mX3Z - mXZ3;
    m[0][4] = mX2YZ - mYZ3;
    m[0][5] = mX3 - mXZ2;
    m[0][6] = mX2Y - mYZ2;
    m[0][7] = mX2Z - mZ3;
    m[0][8] = mX2 - mZ2;

    m[1][1] = d22;
    m[1][2] = mXY3 - mXYZ2;
    m[1][3] = mXY2Z - mXZ3;
    m[1][4] = mY3Z - mYZ3;
    m[1][5] = mXY2 - mXZ2;
    m[1][6] = mY3 - mYZ2;
    m[1][7] = mY2Z - mZ3;
    m[1][8] = mY2 - mZ2;

    m[2][2] = mX2Y2;
    m[2][3] = mX2YZ;
    m[2][4] = mXY2Z;
    m[2][5] = mX2Y;
    m[2][6] = mXY2;
    m[2][7] = mXYZ;
    m[2][8] = mXY;

    m[3][3] = mX2Z2;
    m[3][4] = mXYZ2;
    m[3][5] = mX2Z;
    m[3][6] = mXYZ;
    m[3][7] = mXZ2;
    m[3][8] = mXZ;

    m[4][4] = mY2Z2;
    m[4][5] = mXYZ;
    m[4][6] = mY2Z;
    m[4][7] = mYZ2;
    m[4][8] = mYZ;

    m[5][5] = mX2;  m[5][6] = mXY;  m[5][7] = mXZ;  m[5][8] = mX;
    m[6][6] = mY2;  m[6][7] = mYZ;  m[6][8] = mY;
    m[7][7] = mZ2;  m[7][8] = mZ;
    m[8][8] = n;

    // Mirror the upper triangle; the tenth column is the right-hand side.
    for (uint8_t row = 1; row < 9; row++) {
        for (uint8_t col = 0; col < row; col++) {
            m[row][col] = m[col][row];
        }
    }

    m[0][9] = -3.0 * (mX2Z2 - mZ4);
    m[1][9] = -3.0 * (mY2Z2 - mZ4);
    m[2][9] = -3.0 * mXYZ2;
    m[3][9] = -3.0 * mXZ3;
    m[4][9] = -3.0 * mYZ3;
    m[5][9] = -3.0 * mXZ2;
    m[6][9] = -3.0 * mYZ2;
    m[7][9] = -3.0 * mZ3;
    m[8][9] = -3.0 * mZ2;

    double u[9];
    if (!MagLinearSolve::solve<9>(m, u)) {
        return false;
    }

    double a[3][3];
    a[0][0] = u[0];
    a[1][1] = u[1];
    a[2][2] = 3.0 - u[0] - u[1];
    a[0][1] = a[1][0] = 0.5 * u[2];
    a[0][2] = a[2][0] = 0.5 * u[3];
    a[1][2] = a[2][1] = 0.5 * u[4];

    const double b[3] = { u[5], u[6], u[7] };
    const double c    = u[8];

    // Centre, where the gradient vanishes: A*centre = -b/2.
    const double rhs[3] = { -0.5 * b[0], -0.5 * b[1], -0.5 * b[2] };

    double system[3][4];
    for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            system[row][col] = a[row][col];
        }
        system[row][3] = rhs[row];
    }

    double centre[3];
    if (!MagLinearSolve::solve<3>(system, centre)) {
        return false;
    }

    // About the centre: v' A v = centre' A centre - c =: k
    double k = -c;
    for (uint8_t i = 0; i < 3; i++) {
        for (uint8_t j = 0; j < 3; j++) {
            k += centre[i] * a[i][j] * centre[j];
        }
    }

    if (!(k > 0.0)) {
        return false;
    }

    // Normalised shape matrix: v' S v = 1.
    double shape[3][3];
    for (uint8_t i = 0; i < 3; i++) {
        for (uint8_t j = 0; j < 3; j++) {
            shape[i][j] = a[i][j] / k;
        }
    }

    double vectors[3][3];
    double values[3];
    jacobiEigen(shape, vectors, values);

    // An ellipsoid needs three positive eigenvalues.
    for (uint8_t i = 0; i < 3; i++) {
        if (!(values[i] > 0.0)) {
            return false;
        }
    }

    // Semi-axes are 1/sqrt(eigenvalue); the target sphere's radius is their
    // geometric mean, i.e. equal volume.
    double semi[3];
    for (uint8_t i = 0; i < 3; i++) {
        semi[i] = 1.0 / std::sqrt(values[i]);
    }

    const double radius = std::cbrt(semi[0] * semi[1] * semi[2]) * skNormScale;
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        return false;
    }

    // Correction = radius * S^(1/2) = radius * V sqrt(L) V'. S^(1/2) carries
    // 1/skNormScale and radius carries skNormScale, so the scale cancels.
    double root[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
    for (uint8_t e = 0; e < 3; e++) {
        const double s = std::sqrt(values[e]);
        for (uint8_t i = 0; i < 3; i++) {
            for (uint8_t j = 0; j < 3; j++) {
                root[i][j] += s * vectors[i][e] * vectors[j][e];
            }
        }
    }

    const double gain = radius / skNormScale;

    result.offsetX = static_cast<float>(mRefX + centre[0] * skNormScale);
    result.offsetY = static_cast<float>(mRefY + centre[1] * skNormScale);
    result.offsetZ = static_cast<float>(mRefZ + centre[2] * skNormScale);

    result.m00 = static_cast<float>(gain * root[0][0]);
    result.m01 = static_cast<float>(gain * root[0][1]);
    result.m02 = static_cast<float>(gain * root[0][2]);
    result.m11 = static_cast<float>(gain * root[1][1]);
    result.m12 = static_cast<float>(gain * root[1][2]);
    result.m22 = static_cast<float>(gain * root[2][2]);

    result.radius = static_cast<float>(radius);

    // Longest first.
    for (uint8_t i = 0; i < 3; i++) {
        for (uint8_t j = i + 1; j < 3; j++) {
            if (semi[j] > semi[i]) {
                const double tmp = semi[i];
                semi[i] = semi[j];
                semi[j] = tmp;
            }
        }
    }

    for (uint8_t i = 0; i < 3; i++) {
        result.semiAxis[i] = static_cast<float>(semi[i] * skNormScale);
    }

    return true;
}

void MagEllipsoidFit::jacobiEigen(const double a[3][3], double vectors[3][3],
        double values[3])
{
    double work[3][3];
    for (uint8_t i = 0; i < 3; i++) {
        for (uint8_t j = 0; j < 3; j++) {
            work[i][j]    = a[i][j];
            vectors[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }

    // A symmetric 3x3 converges in a few sweeps; the cap is a backstop.
    for (uint8_t sweep = 0; sweep < 24; sweep++) {

        const double off = std::fabs(work[0][1]) +
                           std::fabs(work[0][2]) +
                           std::fabs(work[1][2]);
        if (off < 1e-18) {
            break;
        }

        for (uint8_t p = 0; p < 2; p++) {
            for (uint8_t q = p + 1; q < 3; q++) {

                if (std::fabs(work[p][q]) < 1e-20) {
                    continue;
                }

                const double theta = (work[q][q] - work[p][p]) /
                                     (2.0 * work[p][q]);
                const double sign  = (theta >= 0.0) ? 1.0 : -1.0;
                const double t     = sign /
                                     (std::fabs(theta) +
                                      std::sqrt(theta * theta + 1.0));
                const double cs    = 1.0 / std::sqrt(t * t + 1.0);
                const double sn    = t * cs;

                for (uint8_t i = 0; i < 3; i++) {
                    const double aip = work[i][p];
                    const double aiq = work[i][q];
                    work[i][p] = cs * aip - sn * aiq;
                    work[i][q] = sn * aip + cs * aiq;
                }

                for (uint8_t i = 0; i < 3; i++) {
                    const double api = work[p][i];
                    const double aqi = work[q][i];
                    work[p][i] = cs * api - sn * aqi;
                    work[q][i] = sn * api + cs * aqi;
                }

                for (uint8_t i = 0; i < 3; i++) {
                    const double vip = vectors[i][p];
                    const double viq = vectors[i][q];
                    vectors[i][p] = cs * vip - sn * viq;
                    vectors[i][q] = sn * vip + cs * viq;
                }
            }
        }
    }

    for (uint8_t i = 0; i < 3; i++) {
        values[i] = work[i][i];
    }
}
