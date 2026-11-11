/**
 ******************************************************************************
 * @file    SampleRateAdapter.cpp
 * @brief   Sample rate adapter
 *
 ******************************************************************************
 */

#include "SensorLayer/Core/SampleRateAdapter.hpp"

namespace Sensor
{

SampleRateAdapter::SampleRateAdapter(uint64_t expectedPeriod)
    : mExpectedPeriod(expectedPeriod)
    , mLastEmitedTimestamp(0)
    , mCount(0)
    , mStepUs(0)
    , mStarted(false)
    , mByStamps(false)
{
}

void SampleRateAdapter::setExpectedPeriod(uint64_t us)
{
    mExpectedPeriod = us;
}

void SampleRateAdapter::reset()
{
    mStarted = false;
}

uint64_t SampleRateAdapter::periodUs(float periodMs)
{
    return static_cast<uint64_t>(periodMs * 1000.0f + 0.5f);
}

uint64_t SampleRateAdapter::divisor(uint64_t periodUs, uint32_t stepUs)
{
    const uint64_t n = periodUs / stepUs;

    return (n != 0) ? n : 1;
}

uint32_t SampleRateAdapter::outputStepUs(uint64_t periodUs, uint32_t stepUs)
{
    if (stepUs == 0) {
        return 0;
    }

    const uint64_t out = divisor(periodUs, stepUs) * stepUs;

    return (out <= UINT32_MAX) ? static_cast<uint32_t>(out) : 0;
}

bool SampleRateAdapter::shouldEmit(uint64_t now, uint32_t stepUs)
{
    if (stepUs == 0) {
        // Nothing to count against. Forget the step, so that one reported
        // later starts the count over.
        mStepUs = 0;
        return true;
    }

    const uint64_t n = divisor(mExpectedPeriod, stepUs);

    if (mStarted && (mStepUs != 0) && (now >= mLastEmitedTimestamp)) {
        const uint64_t sinceUs = now - mLastEmitedTimestamp;

        if (stepUs != mStepUs) {
            mStepUs   = stepUs;
            mByStamps = true;
        }

        if (mByStamps) {
            // What was counted is in steps of the old length, and what comes
            // right after the change can still be spaced by it. Until the
            // next delivery the count is the time since the last one, in
            // whole steps of the new length and to the nearest.
            mCount = (2 * sinceUs + stepUs) / (2 * static_cast<uint64_t>(stepUs));

            if (mCount < n) {
                return false;
            }
        } else {
            // The N-th sample is due N steps after the last delivery; one
            // that is overdue by half a step more means frames were lost on
            // the way. Compared doubled, so the half step needs no rounding.
            const bool gap = (sinceUs * 2 >= (2 * n + 1) * stepUs);

            if (!gap && (++mCount < n)) {
                return false;
            }
        }
    }

    mStarted             = true;
    mByStamps            = false;
    mStepUs              = stepUs;
    mCount               = 0;
    mLastEmitedTimestamp = now;

    return true;
}

} // namespace Sensor
