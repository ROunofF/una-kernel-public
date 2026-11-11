/**
 ******************************************************************************
 * @file    Fsm.hpp
 * @brief   Activity finite state machine for the Daily Health subsystem.
 *
 *          Drives sensor activation policy based on touch/motion/steps/HR
 *          features. Talks to the data layer only via IFeatureSource and
 *          ISensorControl - has no knowledge of concrete sensor drivers.
 *
 *  ============================================================================
 *  STATE OVERVIEW
 *  ============================================================================
 *
 *  Per-state sensor map and timer-based transitions. "burst" means
 *  periodic-on for a short probe (see tickHrProbe and skHrProbe*Ms);
 *  "continuous" means held on while the state is active.
 *
 *  +---------------+--------------------------------------------------------+
 *  | State         | Sensors                                                |
 *  +---------------+--------------------------------------------------------+
 *  | IDLE          | all OFF (entry pass - disconnects everything, then    |
 *  |               | unconditionally advances to UNWORN on the next tick). |
 *  +---------------+--------------------------------------------------------+
 *  | UNWORN        | Touch ON, Motion ON                                   |
 *  |               | Pedo / HR / Floor OFF                                 |
 *  |               | (No contact => no use for steps or HR.)                |
 *  +---------------+--------------------------------------------------------+
 *  | STATIONARY    | Touch ON, Motion ON, Pedo ON                          |
 *  |               | HR burst (period skHrPeriodStationaryMs ~ 5 min,      |
 *  |               |   probe skHrProbeMin..skHrProbeMaxMs ~ 20-45 s,       |
 *  |               |   adaptive early-exit on isHrValidLastMinute)         |
 *  |               | Floor OFF                                             |
 *  +---------------+--------------------------------------------------------+
 *  | LIGHT_ACTIVE  | Touch ON, Motion ON, Pedo ON                          |
 *  |               | HR burst (period skHrPeriodLightActiveMs ~ 2 min,     |
 *  |               |   same probe sizing as STATIONARY)                    |
 *  |               | Floor OFF                                             |
 *  +---------------+--------------------------------------------------------+
 *  | ACTIVE        | Touch ON, Motion ON, Pedo ON,                         |
 *  |               | HR continuous, Floor ON                               |
 *  +---------------+--------------------------------------------------------+
 *
 *  The ActivityTracker (active-minutes aggregator) is not part of this
 *  state map: it runs continuously inside FeatureCenter, fed by the
 *  same motion + step streams the FSM already needs.
 *
 *  ============================================================================
 *  TRANSITIONS
 *  ============================================================================
 *
 *  Symbols:
 *    touch       = mFeatures.touchRaw()
 *    motion      = mFeatures.motionRaw()
 *    !touched(N) = !mFeatures.isActuallyTouched()  AND in-state >= N s
 *    noMotion(N) = mFeatures.isNoMotion(N seconds)
 *    isMotion(N) = mFeatures.isMotion(N seconds)
 *    steps       = mFeatures.stepsLastMinute()
 *    hrValid     = mFeatures.isHrValidLastMinute()
 *
 *  IDLE          -> UNWORN         : unconditional after IDLE-enter pass.
 *
 *  UNWORN        -> STATIONARY     : touch && motion.
 *
 *  STATIONARY    -> UNWORN         : !touched(60)  OR
 *                                   touch && noMotion(15*60) && steps==0
 *                                                 && !hrValid       (fake-touch).
 *  STATIONARY    -> LIGHT_ACTIVE   : touch && (isMotion(30) || steps > 0).
 *
 *  LIGHT_ACTIVE  -> UNWORN         : !touched(60)  OR
 *                                   touch && noMotion(180) && steps==0
 *                                                 && !hrValid       (fake-touch).
 *  LIGHT_ACTIVE  -> STATIONARY     : touch && noMotion(180) && steps==0
 *                                                 && hrValid (settled).
 *  LIGHT_ACTIVE  -> ACTIVE         : touch && steps >= 20.
 *
 *  ACTIVE        -> UNWORN         : !touched(60)  OR
 *                                   touch && noMotion(180) && steps==0
 *                                                 && !hrValid       (fake-touch).
 *  ACTIVE        -> LIGHT_ACTIVE   : touch && steps < 20 && in-state >= 120 s
 *                                   (covers slow-down and full stop).
 *
 *  ============================================================================
 *  WAKE-UP
 *  ============================================================================
 *
 *  Activity::Service ticks the FSM on a coarse periodic timer (~30 s). Touch
 *  and motion *rising* edges from FeatureCenter post an extra tick via
 *  IFsmTrigger so wear-detect transitions feel immediate. Falling
 *  edges and step events ride the next periodic tick - none of the
 *  transitions above shorten on a falling edge.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FSM_HPP
#define COMPONENT_ACTIVITY_FSM_HPP

#include <array>
#include <cstdint>
#include <cstddef>

#include "Activity/FsmInterfaces.hpp"

namespace Activity
{

class Fsm
{
public:
    enum class State : uint8_t
    {
        IDLE = 0,      // All sensors off
        UNWORN,        // Watch not on wrist
        STATIONARY,    // On wrist, no movement
        LIGHT_ACTIVE,  // On wrist, occasional movement
        ACTIVE,        // Walking or significant activity
        COUNT
    };

    Fsm(IFeatureSource& features, ISensorControl& sensors);

    /**
     * @brief Reset state machine to IDLE without touching sensors.
     *
     * The next @ref tick from IDLE will run the IDLE-enter handler
     * (disconnects all sensors) and then advance to UNWORN. Useful when
     * the owner re-initialises the subsystem and wants a clean slate.
     */
    void reset();

    /**
     * @brief Run one FSM cycle. Must be called from a single task
     *        (the Daily Health Manager thread).
     */
    void tick();

    State current() const { return mState; }

private:
    using Handler = State (Fsm::*)();

    IFeatureSource&  mFeatures;
    ISensorControl&  mSensors;

    State    mState;
    uint32_t mStateTimestamp;
    bool     mStateInit;
    uint32_t mHRMeasurementStartTime;

    static const std::array<Handler, static_cast<size_t>(State::COUNT)> kStateTable;

    State stateIdle();
    State stateUnworn();
    State stateStationary();
    State stateLightActive();
    State stateActive();

    bool isTimeInState(uint32_t sec) const;

    /**
     * @brief Periodic HR probe scheduler.
     *
     * Manages connect/disconnect of the HR sensor for states where HR
     * is sampled in bursts (STATIONARY, LIGHT_ACTIVE) instead of being
     * continuously on. The probe runs for at least @ref skHrProbeMinMs
     * (PPG settle time) and is cut short the moment the tracker reports
     * a valid trailing-minute window - typically saving 15-25 s per
     * burst when the SensorLayer already keeps PPG warm. A hard cap of
     * @ref skHrProbeMaxMs guards against the rare case where validity
     * never converges.
     *
     * @param periodMs  Time between burst starts.
     */
    void tickHrProbe(uint32_t periodMs);

    /// Burst cadence per state - cheaper periods where the user is
    /// less likely to want a fresh reading right now.
    static constexpr uint32_t skHrPeriodStationaryMs   = 5u * 60u * 1000u;
    static constexpr uint32_t skHrPeriodLightActiveMs  = 2u * 60u * 1000u;

    /// Adaptive HR probe duration: at least the PPG settle time, at
    /// most a hard cap. Inside the window the probe stops as soon as
    /// IFeatureSource::isHrValidLastMinute() reports valid data.
    ///
    /// Note on granularity: the probe is only re-evaluated on FSM
    /// ticks (skFsmTickMs in Manager, currently 30 s). The effective
    /// stop boundary is therefore quantised - a probe whose ideal
    /// stop falls between two ticks runs until the next one. Worst
    /// case actual probe duration is roughly skHrProbeMaxMs +
    /// fsm_tick_period; not worth adding a dedicated timer for, but
    /// keep it in mind if the cadence is ever changed.
    static constexpr uint32_t skHrProbeMinMs = 20u * 1000u;
    static constexpr uint32_t skHrProbeMaxMs = 45u * 1000u;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FSM_HPP
