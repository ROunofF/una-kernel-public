/**
 ******************************************************************************
 * @file    MotionTracker.hpp
 * @brief   Tracks the motion-detect sensor's on/off state and the
 *          timestamp of the last edge.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FEATURE_MOTION_TRACKER_HPP
#define COMPONENT_ACTIVITY_FEATURE_MOTION_TRACKER_HPP

#include <cstdint>

namespace Activity
{

class MotionTracker
{
public:
    /// Push the latest detect/no-detect from the sensor.
    void onUpdate(bool detected, uint32_t tsMs);

    /// Last known raw value.
    bool raw() const { return mDetected; }

    /// True if motion is currently detected and has been continuously
    /// detected for at least @p sec seconds.
    bool isMotion(uint32_t sec, uint32_t nowMs) const;

    /// True if motion is currently NOT detected and has been absent
    /// for at least @p sec seconds.
    bool isNoMotion(uint32_t sec, uint32_t nowMs) const;

private:
    bool     mSeeded   = false;  ///< First sample seen yet (seeds mLastTsMs).
    bool     mDetected = false;
    uint32_t mLastTsMs = 0;      ///< Timestamp of the last state change (edge).
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FEATURE_MOTION_TRACKER_HPP
