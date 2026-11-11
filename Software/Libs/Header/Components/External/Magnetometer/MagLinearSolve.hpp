/**
 ******************************************************************************
 * @file    MagLinearSolve.hpp
 * @brief   Gaussian elimination for the magnetometer calibration fits.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_LINEAR_SOLVE_HPP
#define COMPONENT_MAG_LINEAR_SOLVE_HPP

#include <cmath>
#include <cstdint>

/**
 * @brief   Gaussian elimination with partial pivoting.
 */
class MagLinearSolve
{

public:

    /**
     * @brief   Solve an N x N linear system.
     * @param   a: The system augmented with its right-hand side, N rows of
     *          N + 1 columns. Eliminated in place.
     * @param   x: Receives the N unknowns; untouched on failure.
     * @retval  'false' if the system is singular at the scale of its own
     *          entries.
     */
    template <uint8_t N>
    static bool solve(double a[][N + 1], double x[])
    {
        // Relative threshold: the moments grow with sample count and field.
        double scale = 0.0;
        for (uint8_t row = 0; row < N; row++) {
            for (uint8_t col = 0; col < N; col++) {
                const double mag = std::fabs(a[row][col]);
                if (mag > scale) {
                    scale = mag;
                }
            }
        }

        if (scale == 0.0) {
            return false;
        }

        const double eps = scale * 1e-12;

        for (uint8_t col = 0; col < N; col++) {

            uint8_t pivot = col;
            for (uint8_t row = col + 1; row < N; row++) {
                if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) {
                    pivot = row;
                }
            }

            if (std::fabs(a[pivot][col]) < eps) {
                return false;
            }

            if (pivot != col) {
                for (uint8_t k = 0; k <= N; k++) {
                    const double tmp = a[col][k];
                    a[col][k]   = a[pivot][k];
                    a[pivot][k] = tmp;
                }
            }

            for (uint8_t row = col + 1; row < N; row++) {
                const double factor = a[row][col] / a[col][col];
                for (uint8_t k = col; k <= N; k++) {
                    a[row][k] -= factor * a[col][k];
                }
            }
        }

        for (int row = N - 1; row >= 0; row--) {
            double sum = a[row][N];
            for (uint8_t col = static_cast<uint8_t>(row + 1); col < N; col++) {
                sum -= a[row][col] * x[col];
            }
            x[row] = sum / a[row][row];
        }

        return true;
    }

};

#endif // COMPONENT_MAG_LINEAR_SOLVE_HPP
