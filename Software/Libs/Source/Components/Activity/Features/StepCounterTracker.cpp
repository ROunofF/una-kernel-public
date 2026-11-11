/**
 ******************************************************************************
 * @file    StepCounterTracker.cpp
 ******************************************************************************
 */

#include "Activity/Features/StepCounterTracker.hpp"

namespace Activity
{

void StepCounterTracker::reset(uint32_t total, uint32_t tsMs)
{
    mWindow.clear();
    mWindow.push_back({tsMs, total});
}

void StepCounterTracker::onUpdate(uint32_t total, uint32_t tsMs)
{
    // First sample seeds the window.
    if (mWindow.empty()) {
        mWindow.push_back({tsMs, total});
        return;
    }

    const Sample& last = mWindow.back();

    // Rollback (counter reset on the IMU side, restore-from-backup,
    // corruption): re-seed the window.
    if (total < last.steps) {
        reset(total, tsMs);
        return;
    }

    const uint32_t dtMs   = tsMs - last.tsMs;
    const uint32_t dSteps = total - last.steps;

    // Unrealistic jump (sync, glitch) - re-seed instead of polluting
    // the sliding average.
    if (dtMs > 0) {
        const uint32_t stepsPerMin = (dSteps * 60000u) / dtMs;
        if (stepsPerMin > skMaxStepsPerMinute) {
            reset(total, tsMs);
            return;
        }
    }

    mWindow.push_back({tsMs, total});

    // Drop entries that fell out of the trailing minute.
    const uint32_t cutoff = (tsMs > skWindowMs) ? (tsMs - skWindowMs) : 0;
    while (!mWindow.empty() && mWindow.front().tsMs < cutoff) {
        mWindow.pop_front();
    }
}

uint32_t StepCounterTracker::lastMinute(uint32_t nowMs) const
{
    if (mWindow.empty()) {
        return 0;
    }

    const uint32_t cutoff = (nowMs > skWindowMs) ? (nowMs - skWindowMs) : 0;

    // Pick the baseline sample: the most recent one at or before
    // cutoff. This anchors the delta to "where the counter stood when
    // the trailing minute began" - important for the moment the user
    // stops walking. Without it, when no fresh pedo events arrive in
    // the last 60 s, every sample left in the window is past cutoff
    // and the delta collapses to 0, leaving the FSM stuck in ACTIVE.
    //
    // Window is sorted ascending by tsMs.
    const Sample* baseline = nullptr;
    for (size_t i = 0; i < mWindow.size(); ++i) {
        const Sample& s = mWindow[i];
        if (s.tsMs <= cutoff) {
            baseline = &s;        // remember the most recent one
        } else {
            break;                // past cutoff, stop scanning
        }
    }

    // If no sample sits at or before cutoff, the window started
    // inside the trailing minute. Treat its oldest entry as the
    // baseline - that is the earliest known counter value within
    // the minute we are reporting on.
    if (!baseline) {
        baseline = &mWindow[0];
    }

    const uint32_t latest = mWindow.back().steps;
    return (latest >= baseline->steps) ? (latest - baseline->steps) : 0;
}

} // namespace Activity
