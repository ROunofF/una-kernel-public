/**
 ******************************************************************************
 * @file    StepCounterTracker.hpp
 * @brief   Sliding-window steps-per-minute tracker.
 *
 *          Sees the cumulative step counter from the IMU; the only
 *          query a consumer cares about is "how many steps in the last
 *          minute". Filters out unrealistic deltas (sensor sync /
 *          counter rollback).
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FEATURE_STEP_COUNTER_TRACKER_HPP
#define COMPONENT_ACTIVITY_FEATURE_STEP_COUNTER_TRACKER_HPP

#include <cstdint>

#include "Activity/RingBuffer.hpp"

namespace Activity
{

class StepCounterTracker
{
public:
    /// Push the latest cumulative step count from the IMU.
    void onUpdate(uint32_t total, uint32_t tsMs);

    /// Caller-driven counter reset (e.g. day rollover, restore).
    void reset(uint32_t total, uint32_t tsMs);

    /// Steps observed in the trailing minute (cumulative-delta over
    /// the sliding window).
    uint32_t lastMinute(uint32_t nowMs) const;

private:
    struct Sample
    {
        uint32_t tsMs;
        uint32_t steps;
    };

    // ImuSubSensorStepCounter publishes at up to 2 Hz (500 ms min
    // period), so a full 60 s window holds ~120 samples. The ring must
    // outsize that: push_back overwrites the oldest entry when full, so
    // an undersized buffer silently drops still-in-window samples before
    // the 60 s trim runs. That shrinks the effective window and makes
    // lastMinute() under-report - the cadence path then never reaches
    // its 100 spm threshold.
    RingBuffer<Sample, 160> mWindow;

    static constexpr uint32_t skWindowMs          = 60000;
    static constexpr uint32_t skMaxStepsPerMinute = 400;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FEATURE_STEP_COUNTER_TRACKER_HPP
