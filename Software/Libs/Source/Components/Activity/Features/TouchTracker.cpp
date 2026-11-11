/**
 ******************************************************************************
 * @file    TouchTracker.cpp
 ******************************************************************************
 */

#include "Activity/Features/TouchTracker.hpp"

namespace Activity
{

void TouchTracker::onUpdate(bool touched, uint32_t tsMs)
{
    mDetected = touched;
    mLastTsMs = tsMs;
}

bool TouchTracker::isActuallyTouched(uint32_t nowMs) const
{
    if (mDetected) {
        return true;
    }
    const uint32_t timeSinceNoTouch = nowMs - mLastTsMs;
    return timeSinceNoTouch > skNoTouchTimeout;
}

} // namespace Activity
