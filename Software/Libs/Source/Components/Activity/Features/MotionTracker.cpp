/**
 ******************************************************************************
 * @file    MotionTracker.cpp
 ******************************************************************************
 */

#include "Activity/Features/MotionTracker.hpp"

namespace Activity
{

void MotionTracker::onUpdate(bool detected, uint32_t tsMs)
{
    // mLastTsMs records the timestamp of the last *edge* (state change),
    // which is what isMotion()/isNoMotion() integrate against to measure
    // "continuously detected/absent for >= N s". Only a change of state is
    // an edge: the motion-detect sub-sensor re-publishes SIG_MOTION as a
    // ~5 s heartbeat for the whole duration of sustained motion (see
    // ImuSubSensorMotionDetect::handleMotionEvent), and those same-state
    // republishes must NOT reset the dwell clock - otherwise the "for at
    // least N s" check could never be satisfied during real motion.
    //
    // The first sample seeds the clock so a measurement taken right after
    // the sensor connects is anchored to that moment, not to time 0.
    if (!mSeeded || detected != mDetected) {
        mLastTsMs = tsMs;
    }
    mDetected = detected;
    mSeeded   = true;
}

bool MotionTracker::isMotion(uint32_t sec, uint32_t nowMs) const
{
    return mDetected && (nowMs - mLastTsMs) > (sec * 1000);
}

bool MotionTracker::isNoMotion(uint32_t sec, uint32_t nowMs) const
{
    return mDetected ? false : (nowMs - mLastTsMs) > (sec * 1000);
}

} // namespace Activity
