/**
 ******************************************************************************
 * @file    IDurationListener.hpp
 * @brief   Callback for consumers that want to be notified when the
 *          accumulated active minutes change.
 *
 *          Activity::Service fires the callback after every FSM tick
 *          whenever ActivityTracker produces a new minute total
 *          (no-change ticks are suppressed). The expected consumer is
 *          a SensorLayer-side sensor (e.g. Sensor::ActivityTime) that
 *          forwards the value to BLE / UI listeners on its own driver
 *          channel. Producer side (Service, Tracker) stays unaware of
 *          its consumers.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_IDURATION_LISTENER_HPP
#define COMPONENT_ACTIVITY_IDURATION_LISTENER_HPP

#include <cstdint>

namespace Activity
{

class IDurationListener
{
public:
    virtual ~IDurationListener() = default;

    /**
     * @brief Called when the cumulative active-minutes counter has
     *        advanced. @p minutesSinceBoot is monotonic from system
     *        boot and never resets; any daily cut is a downstream
     *        concern.
     */
    virtual void onActivityDurationChanged(uint32_t minutesSinceBoot) = 0;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_IDURATION_LISTENER_HPP
