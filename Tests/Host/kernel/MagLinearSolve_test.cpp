/**
 ******************************************************************************
 * @file    MagLinearSolve_test.cpp
 * @brief   Host tests for the Gaussian elimination the calibration fits share:
 *          solving, pivoting, refusing a singular system without touching the
 *          answer, and a singularity test that follows the system's scale.
 *          The fits' own tests stay green if the refusal breaks.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>

#include "MagLinearSolve.hpp"

TEST(MagLinearSolve, SolvesAKnownSystem)
{
    double a[3][4] = {
        {  2.0,  1.0, -1.0,   8.0 },
        { -3.0, -1.0,  2.0, -11.0 },
        { -2.0,  1.0,  2.0,  -3.0 },
    };
    double x[3] = {};

    ASSERT_TRUE(MagLinearSolve::solve<3>(a, x));

    EXPECT_NEAR(x[0],  2.0, 1e-12);
    EXPECT_NEAR(x[1],  3.0, 1e-12);
    EXPECT_NEAR(x[2], -1.0, 1e-12);
}

// Without swapping rows the first step divides by the zero.
TEST(MagLinearSolve, PivotsPastAZeroOnTheDiagonal)
{
    double a[3][4] = {
        { 0.0, 1.0, 1.0, 5.0 },
        { 1.0, 0.0, 1.0, 4.0 },
        { 1.0, 1.0, 0.0, 3.0 },
    };
    double x[3] = {};

    ASSERT_TRUE(MagLinearSolve::solve<3>(a, x));

    EXPECT_NEAR(x[0], 1.0, 1e-12);
    EXPECT_NEAR(x[1], 2.0, 1e-12);
    EXPECT_NEAR(x[2], 3.0, 1e-12);
}

// The second row is twice the first.
TEST(MagLinearSolve, RefusesASingularSystemAndLeavesTheAnswerAlone)
{
    double a[3][4] = {
        { 1.0, 2.0, 3.0, 1.0 },
        { 2.0, 4.0, 6.0, 2.0 },
        { 1.0, 1.0, 1.0, 3.0 },
    };
    double x[3] = { 7.0, 7.0, 7.0 };

    EXPECT_FALSE(MagLinearSolve::solve<3>(a, x));

    EXPECT_EQ(x[0], 7.0);
    EXPECT_EQ(x[1], 7.0);
    EXPECT_EQ(x[2], 7.0);
}

// Well conditioned but all tiny: a fixed epsilon would call it singular.
TEST(MagLinearSolve, TheSingularityTestFollowsTheScaleOfTheSystem)
{
    constexpr double k = 1e-20;

    double a[3][4] = {
        { k,   0.0, 0.0, 1.0 * k },
        { 0.0, k,   0.0, 2.0 * k },
        { 0.0, 0.0, k,   3.0 * k },
    };
    double x[3] = {};

    ASSERT_TRUE(MagLinearSolve::solve<3>(a, x));

    EXPECT_NEAR(x[0], 1.0, 1e-9);
    EXPECT_NEAR(x[1], 2.0, 1e-9);
    EXPECT_NEAR(x[2], 3.0, 1e-9);
}

// Diagonally dominant, with the right-hand side built from a known answer.
TEST(MagLinearSolve, SolvesTheNineByNineTheEllipsoidUses)
{
    double expected[9];
    double a[9][10];

    for (int i = 0; i < 9; i++) {
        expected[i] = static_cast<double>(i) - 4.0;
    }

    for (int row = 0; row < 9; row++) {
        double rhs = 0.0;

        for (int col = 0; col < 9; col++) {
            a[row][col] = (row == col) ? 20.0 : 1.0 / (1.0 + row + col);
            rhs += a[row][col] * expected[col];
        }

        a[row][9] = rhs;
    }

    double x[9] = {};

    ASSERT_TRUE(MagLinearSolve::solve<9>(a, x));

    for (int i = 0; i < 9; i++) {
        EXPECT_NEAR(x[i], expected[i], 1e-9) << "unknown " << i;
    }
}
