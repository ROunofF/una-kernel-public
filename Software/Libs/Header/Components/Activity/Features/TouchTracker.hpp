/**
 ******************************************************************************
 * @file    TouchTracker.hpp
 * @brief   Tracks the wrist-touch sensor's state.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FEATURE_TOUCH_TRACKER_HPP
#define COMPONENT_ACTIVITY_FEATURE_TOUCH_TRACKER_HPP

#include <cstdint>

namespace Activity
{

class TouchTracker
{
public:
    /// Push the latest touch/no-touch from the sensor.
    void onUpdate(bool touched, uint32_t tsMs);

    /// Last known raw value.
    bool raw() const { return mDetected; }

    /// True if the sensor reports touch, OR a recent no-touch reading
    /// is still inside the de-bounce window - used to ride out brief
    /// loss-of-contact glitches without flapping the FSM into UNWORN.
    bool isActuallyTouched(uint32_t nowMs) const;

private:
    bool     mDetected = false;
    uint32_t mLastTsMs = 0;

    static constexpr uint32_t skNoTouchTimeout = 1 * 60 * 1000;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FEATURE_TOUCH_TRACKER_HPP
