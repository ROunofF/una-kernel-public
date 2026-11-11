/**
 ******************************************************************************
 * @file    MagCalibrator.cpp
 * @brief   The two halves of a magnetometer calibration run.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagCalibrator.hpp"

#include <cmath>

MagCalibrator::MagCalibrator()
    : mSphere()
    , mEllipsoid()
    , mLimits()
    , mHaveSphere(false)
    , mHaveEllipsoid(false)
    , mSphereResult()
    , mEllipsoidResult()
    , mKept()
    , mKeptCount(0)
    , mKeepOver(skKeptTarget)
    , mKeepCredit(0)
    , mCoverage(0)
{
}

void MagCalibrator::setLimits(const MagCalibration::Limits& limits)
{
    mLimits = limits;
}

const MagCalibration::Limits& MagCalibrator::getLimits() const
{
    return mLimits;
}

void MagCalibrator::start(uint32_t expectedSamples)
{
    mSphere.reset();
    mEllipsoid.reset();

    mHaveSphere    = false;
    mHaveEllipsoid = false;

    mSphereResult    = MagSphereFit::Result();
    mEllipsoidResult = MagEllipsoidFit::Result();

    mKeptCount  = 0;
    mKeepOver   = (expectedSamples > skKeptTarget) ? expectedSamples
                                                 : skKeptTarget;
    mKeepCredit = 0;
    mCoverage   = 0;
}

void MagCalibrator::add(float x, float y, float z)
{
    mSphere.add(x, y, z);
    mEllipsoid.add(x, y, z);

    mKeepCredit += skKeptTarget;

    if (mKeepCredit < mKeepOver) {
        return;
    }

    mKeepCredit -= mKeepOver;

    if (mKeptCount == skKeptSamples) {
        for (uint16_t i = 0; i < (skKeptSamples / 2U); i++) {
            mKept[i][0] = mKept[2U * i][0];
            mKept[i][1] = mKept[2U * i][1];
            mKept[i][2] = mKept[2U * i][2];
        }

        mKeptCount = skKeptSamples / 2U;
        mKeepOver *= 2U;
    }

    mKept[mKeptCount][0] = x;
    mKept[mKeptCount][1] = y;
    mKept[mKeptCount][2] = z;
    mKeptCount++;
}

MagCalibrator::Progress MagCalibrator::getProgress() const
{
    Progress progress;

    progress.samples = mSphere.getCount();
    progress.spanX   = mSphere.getSpanX();
    progress.spanY   = mSphere.getSpanY();
    progress.spanZ   = mSphere.getSpanZ();

    return progress;
}

MagCalibration::Verdict MagCalibrator::finish(MagCalibration::Record& record)
{
    mHaveSphere    = mSphere.solve(mSphereResult);
    mHaveEllipsoid = mEllipsoid.solve(mEllipsoidResult);
    mCoverage      = mHaveSphere ? countCoveredDirections() : 0U;

    if (!mHaveSphere || !mHaveEllipsoid) {
        // Too few samples means keep collecting; enough samples that fit no
        // shape means turn the watch differently.
        return (mSphere.getCount() < mLimits.minSamples)
                       ? MagCalibration::Verdict::TOO_FEW_SAMPLES
                       : MagCalibration::Verdict::COVERAGE_TOO_NARROW;
    }

    MagCalibration::Record candidate;

    candidate.offsetX = mEllipsoidResult.offsetX;
    candidate.offsetY = mEllipsoidResult.offsetY;
    candidate.offsetZ = mEllipsoidResult.offsetZ;

    candidate.m00 = mEllipsoidResult.m00;
    candidate.m01 = mEllipsoidResult.m01;
    candidate.m02 = mEllipsoidResult.m02;
    candidate.m11 = mEllipsoidResult.m11;
    candidate.m12 = mEllipsoidResult.m12;
    candidate.m22 = mEllipsoidResult.m22;

    candidate.radius = mEllipsoidResult.radius;

    candidate.residual = mSphereResult.residualRms;
    candidate.samples  = mSphere.getCount();

    record = candidate;

    return MagCalibration::evaluate(record, mCoverage, mLimits);
}

uint8_t MagCalibrator::countCoveredDirections() const
{
    // The sphere's centre, not the ellipsoid's: on a tumble too narrow to pin
    // a shape, the ellipsoid's centre is what goes wrong.
    const float cx = mSphereResult.offsetX;
    const float cy = mSphereResult.offsetY;
    const float cz = mSphereResult.offsetZ;

    static constexpr float kUnit[4] = { 0.0f, 1.0f, 0.70710678f, 0.57735027f };

    // Nearer the centre than half the weakest plausible field is the noise of
    // a watch lying still, from where every direction looks reached.
    const float nearest = 0.5f * mLimits.minRadius;

    uint8_t hits[skDirections] = {};

    for (uint16_t s = 0; s < mKeptCount; s++) {
        const float dx = mKept[s][0] - cx;
        const float dy = mKept[s][1] - cy;
        const float dz = mKept[s][2] - cz;

        if (((dx * dx) + (dy * dy) + (dz * dz)) < (nearest * nearest)) {
            continue;
        }

        uint8_t best    = 0;
        float   bestDot = -1.0e30f;
        uint8_t index   = 0;

        for (int a = -1; a <= 1; a++) {
            for (int b = -1; b <= 1; b++) {
                for (int c = -1; c <= 1; c++) {
                    const int nonZero = (a != 0) + (b != 0) + (c != 0);

                    if (nonZero == 0) {
                        continue;
                    }

                    const float dot = kUnit[nonZero] *
                            (static_cast<float>(a) * dx +
                             static_cast<float>(b) * dy +
                             static_cast<float>(c) * dz);

                    if (dot > bestDot) {
                        bestDot = dot;
                        best    = index;
                    }

                    index++;
                }
            }
        }

        if (hits[best] < skMinPerDirection) {
            hits[best]++;
        }
    }

    uint8_t covered = 0;

    for (uint8_t d = 0; d < skDirections; d++) {
        if (hits[d] >= skMinPerDirection) {
            covered++;
        }
    }

    return covered;
}

uint8_t MagCalibrator::getCoverage() const
{
    return mCoverage;
}

bool MagCalibrator::getEllipsoid(MagEllipsoidFit::Result& result) const
{
    if (!mHaveEllipsoid) {
        return false;
    }

    result = mEllipsoidResult;

    return true;
}

bool MagCalibrator::getSphere(MagSphereFit::Result& result) const
{
    if (!mHaveSphere) {
        return false;
    }

    result = mSphereResult;

    return true;
}

MagCalibrationCheck::MagCalibrationCheck()
    : mCalibration()
    , mCount(0)
    , mSum(0.0)
    , mSumSq(0.0)
    , mMin(0.0f)
    , mMax(0.0f)
{
}

bool MagCalibrationCheck::start(const MagCalibration::Record& record)
{
    // set() keeps the record in force when it refuses, and nothing below
    // runs, so a refused start leaves the check as it was.
    if (!mCalibration.set(record)) {
        return false;
    }

    mCount = 0;
    mSum   = 0.0;
    mSumSq = 0.0;
    mMin   = 0.0f;
    mMax   = 0.0f;

    return true;
}

void MagCalibrationCheck::add(float x, float y, float z)
{
    // Nothing is counted before a start is taken.
    if (!mCalibration.apply(x, y, z)) {
        return;
    }

    const float magnitude = std::sqrt(x * x + y * y + z * z);

    if (mCount == 0) {
        mMin = magnitude;
        mMax = magnitude;
    } else if (magnitude < mMin) {
        mMin = magnitude;
    } else if (magnitude > mMax) {
        mMax = magnitude;
    }

    mCount++;
    mSum   += magnitude;
    mSumSq += static_cast<double>(magnitude) * magnitude;
}

uint32_t MagCalibrationCheck::getCount() const
{
    return mCount;
}

bool MagCalibrationCheck::getResult(Result& result) const
{
    if (mCount == 0) {
        return false;
    }

    const double mean = mSum / mCount;

    double variance = (mSumSq / mCount) - (mean * mean);
    if (variance < 0.0) {
        // Rounding in the cancellation of two large sums.
        variance = 0.0;
    }

    result.samples = mCount;
    result.mean    = static_cast<float>(mean);
    result.spread  = static_cast<float>(std::sqrt(variance));
    result.minimum = mMin;
    result.maximum = mMax;

    return true;
}

bool MagCalibrationCheck::passed(const MagCalibration::Limits& limits,
        uint32_t minSamples) const
{
    if (!mCalibration.isValid()) {
        return false;
    }

    if (mCount < minSamples) {
        return false;
    }

    Result result;
    if (!getResult(result)) {
        return false;
    }

    // Judged against the field the calibration expects, never against this
    // run's own numbers, which would move the goalposts with the output.
    const float radius  = mCalibration.get().radius;
    const float allowed = limits.maxCorrectedSpreadFactor * radius;

    // Scatter with direction: leftover hard or soft iron.
    if (result.spread > allowed) {
        return false;
    }

    // Level: a uniformly wrong scale is consistent, so its scatter is zero.
    return std::fabs(result.mean - radius) <= allowed;
}
