/**
 ******************************************************************************
 * @file    ActivityTracker.cpp
 ******************************************************************************
 */

#include "Activity/Features/ActivityTracker.hpp"

#define LOG_MODULE_PRX      "Activity.Features"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger/Logger.h"

namespace Activity
{

void ActivityTracker::onHrSample(uint8_t bpm, uint8_t trustLevel, uint32_t tsMs)
{
    // Drop samples that the PPG itself flags as unreliable, or hard
    // zeros emitted while the optical front-end is still settling
    // (see runtime logs: tl=0 / bpm=0 appears for ~5-10 s after
    // each burst start).
    if (trustLevel < skHrMinTrustLevel || bpm == 0) {
        return;
    }

    // Rate-limit: in ACTIVE state HR streams at ~1 Hz; we only want
    // one push per ~2 min so a small 3-slot window represents real
    // 4-6 min of evidence rather than 3 seconds. The first push
    // (mHrSampleCount == 0, i.e. window is empty) is always accepted -
    // we use the count rather than mLastHrSampleMs because a valid
    // first timestamp can legally be zero in tests and at boot.
    if (mHrSampleCount > 0 &&
        (tsMs - mLastHrSampleMs) < skHrSampleMinIntervalMs)
    {
        return;
    }

    pushHrSample(bpm);
    mLastHrSampleMs = tsMs;
}

void ActivityTracker::pushHrSample(uint8_t bpm)
{
    mHrSamples[mHrSampleIdx] = bpm;
    mHrSampleIdx = static_cast<uint8_t>((mHrSampleIdx + 1u) % skHrWindowSize);
    if (mHrSampleCount < skHrWindowSize) {
        ++mHrSampleCount;
    }
}

void ActivityTracker::expireStaleHrWindow(uint32_t nowMs)
{
    // Wear-off / wear-on cycle, or a long gap between bursts: old
    // samples should not keep "qualifying" the HR path. After 5 min
    // without a fresh sample, drop the evidence we have.
    if (mHrSampleCount == 0 || mLastHrSampleMs == 0) {
        return;
    }
    if ((nowMs - mLastHrSampleMs) > skHrWindowStaleMs) {
        mHrSampleCount = 0;
        mHrSampleIdx   = 0;
        // Keep mLastHrSampleMs as-is so the very next onHrSample stays
        // rate-limited against the actual elapsed wall-clock.
    }
}

bool ActivityTracker::hrPathQualified() const
{
    if (mHrSampleCount < skHrWindowSize) {
        return false;
    }
    uint8_t elevated = 0;
    for (uint8_t i = 0; i < mHrSampleCount; ++i) {
        if (mHrSamples[i] >= mHrThresholdBpm) {
            ++elevated;
        }
    }
    return elevated >= skHrQuorum;
}

float ActivityTracker::computeScore(uint32_t stepsLastMin, bool isUserMoving) const
{
    // Cadence path is checked first: it's the simplest, HR-independent
    // evidence. Both paths credit at the same rate, so this is an early
    // out, not a precedence rule.
    if (stepsLastMin >= skCadenceThresholdSpm) {
        return skScoreCadence;
    }
    if (isUserMoving && hrPathQualified()) {
        return skScoreHrMotion;
    }
    return 0.0f;
}

uint32_t ActivityTracker::tick(uint32_t nowMs, bool isUserMoving, uint32_t stepsLastMin)
{
    expireStaleHrWindow(nowMs);

    const float score = computeScore(stepsLastMin, isUserMoving);

    if (mSeeded) {
        // Conservative integration: credit only the lower of the two
        // endpoint scores. A single tick at the boundary of an active
        // period can therefore lose up to one tick worth of credit -
        // acceptable, prevents over-counting transition windows when
        // we don't know exactly when the state actually flipped.
        const float creditScore = (mLastScore < score) ? mLastScore : score;
        if (creditScore > 0.0f) {
            uint32_t dt = nowMs - mLastTickMs;
            if (dt > skMaxCreditableGapMs) {
                // Ticks were not actually delivered across a gap this long
                // (see skMaxCreditableGapMs). The sensors were not observing,
                // so credit only the cap rather than inventing active minutes.
                // The warning makes an over-cap gap a signal that one of those
                // fault paths is real, not something to silently absorb.
                LOG_WARNING("tick gap %ums exceeds cap %ums - clamping\n",
                            dt, skMaxCreditableGapMs);
                dt = skMaxCreditableGapMs;
            }
            mAccumulatedMs += static_cast<uint32_t>(
                static_cast<float>(dt) * creditScore);
        }
    }

    mLastScore  = score;
    mLastTickMs = nowMs;
    mSeeded     = true;

    return mBaselineMinutes + (mAccumulatedMs / 60000u);
}

void ActivityTracker::setHrThresholdBpm(uint8_t bpm)
{
    // Ignore 0: an unset / uninitialised HR-zone value must not drop the
    // cutoff to zero, which would make every trusted sample "elevated"
    // and qualify the HR path continuously. Keep the previous cutoff.
    if (bpm == 0) {
        return;
    }
    mHrThresholdBpm = bpm;
}

void ActivityTracker::reset(uint32_t baselineMinutes, uint32_t tsMs)
{
    mAccumulatedMs   = 0;
    mLastScore       = 0.0f;
    mLastTickMs      = tsMs;
    mBaselineMinutes = baselineMinutes;
    mSeeded          = true;

    // Wipe HR evidence too - a new accounting period should start
    // empty rather than inherit the previous day's tail samples.
    mHrSampleCount  = 0;
    mHrSampleIdx    = 0;
    mLastHrSampleMs = 0;
}

} // namespace Activity
