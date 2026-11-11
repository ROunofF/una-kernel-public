/**
 ******************************************************************************
 * @file    SensorStubFloor.hpp
 * @brief   Floor counter
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FLOOR_COUNTER_HPP
#define __FLOOR_COUNTER_HPP

#include "SensorLayer/Sensors/Floor/LandingDetector.hpp"
#include "Filters/SimpleLPF.hpp"
#include "SwTimer.hpp"

#include "etl/circular_buffer.h"

#include <cstdint>

namespace Sensor {

/**
 * @class FloorCounter
 * @brief Counts floors climbed or descended using barometric altitude
 *        changes gated by pedometer steps.
 *
 * Algorithm overview:
 * - Barometric altitude is smoothed with an exponential moving average (EMA).
 * - Steps are collected in a sliding window to validate altitude changes.
 * - Finite-state machine tracks Idle, Climbing, and Descending states.
 * - A new floor is confirmed when altitude change exceeds confirm threshold
 *   and step gate is satisfied.
 * - Reversals (changing direction mid-segment) are handled by switching
 *   between Climbing and Descending states.
 *
 * Configuration parameters:
 * - floorHeight:        assumed average floor height in meters
 * - startThreshRatio:   fraction of floorHeight required to start a segment
 * - confirmThreshRatio: fraction of floorHeight required to confirm a floor
 * - minStepsWindow:     minimum number of steps required in the sliding window
 * - windowMs:           length of sliding step window in milliseconds
 * - emaAlpha:           smoothing factor for altitude EMA
 */
class FloorCounter {
public:
    struct Params {
        // Configuration
        static constexpr float    floorHeight        = 2.8f;     ///< assumed floor height
        static constexpr float    startThreshRatio   = 0.2f;     ///< ratio of floorHeight to start a segment
        static constexpr float    confirmThreshRatio = 0.9f;     ///< ratio of floorHeight to confirm a floor
        static constexpr uint32_t minStepsWindow     = 5;        ///< minimal steps within windowMs
        static constexpr uint32_t windowMs           = 10000;    ///< sliding window in ms
        static constexpr float    paPerMeter         = 11.9f;    ///< Pascals per meter
        static constexpr float    alphaSignal        = 0.85f;    ///< smoothing factor for the current value
        static constexpr float    alphaBaseline      = 0.99f;    ///< smoothing factor for the Baseline

        // Derived thresholds
        static constexpr float    floorPressure      = floorHeight * paPerMeter;
        static constexpr float    thldStart          = startThreshRatio   * floorPressure;
        static constexpr float    thldConfirm        = confirmThreshRatio * floorPressure;
        static constexpr float    thldRemaining      = (1.0f - confirmThreshRatio) * floorPressure;
    };

    static_assert(Params::startThreshRatio > 0.0f);
    static_assert(Params::startThreshRatio < Params::confirmThreshRatio);
    static_assert(Params::confirmThreshRatio < 1.0f);

    /** @brief Default-construct with Params default values. */
    FloorCounter();

    /** @brief Construct with explicit parameters. */
    explicit FloorCounter(const Params& p);

    enum class State {
        Idle = 0,
        Climbing,
        ClimbingSettling,
        Descending,
        DescendingSettling
    };

    void  resetFSM();

    void  setCounters(uint32_t up, uint32_t down);

    void  newPressure(float pressure, uint32_t ts);
    void  newStep(uint32_t ts);

    uint32_t getFloorsUp()   const { return mFloorsUp;   }
    uint32_t getFloorsDown() const { return mFloorsDown; }

    State state() const { return mState; }

    float getBaseline();
    float getDelta();

private:
    void  updateStepWindow(uint32_t ts_ms);
    bool  stepGateOK(uint32_t ts_ms) const;
    void  executeFSM(uint32_t ts_ms);

    Params               mParams;
    Filter::SimpleLPF    mPressure;         ///< smoothed pressure
    Filter::SimpleLPF    mBaseLine;         ///< baseline altitude fixed in Idle
    State                mState;            ///< FSM state
    /// Timestamps of steps (ms), oldest first, trimmed to windowMs.
    ///
    /// Capacity is set by what a person can physically do in the window
    /// rather than by a guess: windowMs is 10 s and a sprint is around five
    /// steps a second, so 64 is comfortably past any real cadence. The
    /// buffer drops its oldest entry when it wraps, which is what trimming
    /// does to that entry moments later anyway - and the gate only asks
    /// whether at least minStepsWindow steps are inside the window, which
    /// five sixty-fourths of a buffer cannot change.
    ///
    /// Costs 65 slots, not 64: etl::circular_buffer holds MAX_SIZE + 1 so a
    /// full buffer stays distinguishable from an empty one. That applies to
    /// every ETL ring sized from here on, so size them for what is needed
    /// and expect one slot more.
    static constexpr size_t skMaxSteps = 64;

    etl::circular_buffer<uint32_t, skMaxSteps> mSteps;
    uint32_t             mFloorsUp;
    uint32_t             mFloorsDown;
    ::Driver::SwTimer    mMovementTimer;
    LandingDetector      mLandingDetector;
};

} // namespace Sensor

#endif // __FLOOR_COUNTER_HPP
