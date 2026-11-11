/**
 ******************************************************************************
 * @file    MagCalibrationSession.cpp
 * @brief   The arithmetic of one calibration or verification run.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagCalibrationSession.hpp"

#include <cstring>

void MagCalibrationSession::startFit(uint32_t expectedSamples)
{
    mMode = Mode::FIT;
    mCalibrator.start(expectedSamples);
    publishProgress(MagCalibrator::Progress{});
}

bool MagCalibrationSession::startCheck(const MagCalibration::Record& record)
{
    // Refused before a tumble is spent on a correction that cannot be applied.
    if (!mCheck.start(record)) {
        return false;
    }

    mMode = Mode::CHECK;
    publishProgress(MagCalibrator::Progress{});

    return true;
}

void MagCalibrationSession::feed(float x, float y, float z)
{
    if (mMode == Mode::FIT) {
        mCalibrator.add(x, y, z);
        publishProgress(mCalibrator.getProgress());
    } else {
        mCheck.add(x, y, z);

        // A check fits nothing, so it has no spans to report.
        MagCalibrator::Progress progress;
        progress.samples = mCheck.getCount();
        publishProgress(progress);
    }
}

MagCalibrator::Progress MagCalibrationSession::getProgress() const
{
    MagCalibrator::Progress progress;

    progress.samples = mSamples.load();
    progress.spanX   = fromBits(mSpanX.load());
    progress.spanY   = fromBits(mSpanY.load());
    progress.spanZ   = fromBits(mSpanZ.load());

    return progress;
}

MagCalibrationSession::Result MagCalibrationSession::finish(bool completed)
{
    Result result;

    result.mode      = mMode;
    result.completed = completed;

    if (!completed) {
        return result;
    }

    if (mMode == Mode::FIT) {
        result.verdict       = mCalibrator.finish(result.record);
        result.progress      = mCalibrator.getProgress();
        result.coverage      = mCalibrator.getCoverage();
        result.haveSphere    = mCalibrator.getSphere(result.sphere);
        result.haveEllipsoid = mCalibrator.getEllipsoid(result.ellipsoid);
    } else {
        result.progress.samples = mCheck.getCount();
        result.haveCheck        = mCheck.getResult(result.check);
        result.passed           = mCheck.passed(mCalibrator.getLimits(),
                                                skMinCheckSamples);
    }

    return result;
}

void MagCalibrationSession::publishProgress(const MagCalibrator::Progress& progress)
{
    mSamples.store(progress.samples);
    mSpanX.store(toBits(progress.spanX));
    mSpanY.store(toBits(progress.spanY));
    mSpanZ.store(toBits(progress.spanZ));
}

uint32_t MagCalibrationSession::toBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float MagCalibrationSession::fromBits(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
