/**
 ******************************************************************************
 * @file    CadenceEstimator.cpp
 * @brief   Windowed step-counter running cadence estimator (cadence only).
 ******************************************************************************
 */

#include "SensorLayer/Sensors/Cadence/CadenceEstimator.hpp"

namespace Sensor
{

CadenceEstimator::CadenceEstimator()
{
}

void CadenceEstimator::reset()
{
    mHead = 0;
    mSize = 0;
    invalidate();
}

void CadenceEstimator::invalidate()
{
    // Zero the reported SPM alongside the flag so consumers that read SPM
    // without checking cadenceValid see 0 rather than a stale value.
    mCadenceSpm   = 0.0f;
    mCadenceValid = false;
}

CadenceEstimator::CounterSample& CadenceEstimator::newestEntry()
{
    return mRing[(mHead + mSize - 1u) % CadenceConfig::kMaxSamples];
}

const CadenceEstimator::CounterSample& CadenceEstimator::newestEntry() const
{
    return mRing[(mHead + mSize - 1u) % CadenceConfig::kMaxSamples];
}

const CadenceEstimator::CounterSample& CadenceEstimator::oldestEntry() const
{
    return mRing[mHead];
}

void CadenceEstimator::pushEntry(uint64_t tUs, uint32_t count)
{
    if (mSize == CadenceConfig::kMaxSamples) {
        // Ring full: evict the oldest sample to make room.
        mHead = (mHead + 1u) % CadenceConfig::kMaxSamples;
        --mSize;
    }

    mRing[(mHead + mSize) % CadenceConfig::kMaxSamples] = CounterSample{tUs, count};
    ++mSize;
}

void CadenceEstimator::evictOutsideWindow(uint64_t nowUs)
{
    // Drop samples that fell out of the rate window, always keeping the
    // newest one.
    while (mSize > 1u &&
           (oldestEntry().tUs + CadenceConfig::kWindowUs) < nowUs) {
        mHead = (mHead + 1u) % CadenceConfig::kMaxSamples;
        --mSize;
    }
}

void CadenceEstimator::onStepCount(uint64_t timestampUs, uint32_t cumulativeSteps)
{
    if (mSize > 0u) {
        CounterSample& last = newestEntry();

        if (timestampUs < last.tUs || cumulativeSteps < last.count) {
            // Time went backwards or the counter reset/wrapped: rebase on
            // this sample.
            reset();
            pushEntry(timestampUs, cumulativeSteps);
            return;
        }

        if ((timestampUs - last.tUs) > CadenceConfig::kMaxStepGapUs) {
            // Stepping paused then resumed: the pre-gap window would dilute
            // the rate across the pause (and clamp to a spurious valid-low
            // reading). Discard it and re-acquire cleanly from this sample.
            reset();
            pushEntry(timestampUs, cumulativeSteps);
            return;
        }

        if (timestampUs == last.tUs) {
            // Same-ms batch: coalesce into the newest entry so the window
            // never holds zero-span pairs.
            last.count = cumulativeSteps;
            return;
        }
    }

    pushEntry(timestampUs, cumulativeSteps);
}

void CadenceEstimator::tickUs(uint64_t nowUs)
{
    if (mSize == 0u) {
        invalidate();
        return;
    }

    evictOutsideWindow(nowUs);

    const CounterSample& newest = newestEntry();
    const CounterSample& oldest = oldestEntry();

    if ((nowUs > newest.tUs) &&
        ((nowUs - newest.tUs) > CadenceConfig::kMaxStepGapUs)) {
        // No fresh step-counter sample: the wearer stopped stepping.
        invalidate();
        return;
    }

    const uint64_t spanUs = newest.tUs - oldest.tUs;

    if (spanUs < CadenceConfig::kMinAcqWindowUs) {
        // Still acquiring: not enough sample span for a trustworthy rate.
        invalidate();
        return;
    }

    if ((spanUs == 0u) || (newest.count <= oldest.count)) {
        // Belt-and-braces: no time span (div-by-zero) or no step progress.
        invalidate();
        return;
    }

    const float cadenceSpm = static_cast<float>(newest.count - oldest.count) *
                             60.0e6f / static_cast<float>(spanUs);

    if (cadenceSpm < CadenceConfig::kMinCadenceSpm) {
        // Below the running band (e.g. slow walking, or a gap-diluted rate):
        // reject rather than clamp-and-validate, matching the old estimator's
        // band-rejection semantics.
        invalidate();
        return;
    }

    // A genuine near-ceiling runner is clamped to the max and stays valid.
    mCadenceSpm   = (cadenceSpm > CadenceConfig::kMaxCadenceSpm)
                        ? CadenceConfig::kMaxCadenceSpm
                        : cadenceSpm;
    mCadenceValid = true;
}

CadenceEstimator::Sample CadenceEstimator::getSample() const
{
    Sample s{};
    s.cadenceSpm   = mCadenceSpm;
    s.cadenceValid = mCadenceValid;
    return s;
}

} // namespace Sensor
