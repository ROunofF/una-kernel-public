/**
 ******************************************************************************
 * @file    Fsm.cpp
 * @brief   Activity FSM implementation.
 *
 *          Drives the per-state sensor activation policy.
 ******************************************************************************
 */

#include "Activity/Fsm.hpp"

#define LOG_MODULE_PRX      "Activity.Fsm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Activity
{

const std::array<Fsm::Handler, static_cast<size_t>(Fsm::State::COUNT)>
    Fsm::kStateTable = {
        &Fsm::stateIdle,
        &Fsm::stateUnworn,
        &Fsm::stateStationary,
        &Fsm::stateLightActive,
        &Fsm::stateActive
    };

namespace {

// Human-readable state names for logging, indexed by Fsm::State.
const char* const kStateNames[] = {
    "IDLE",
    "UNWORN",
    "STATIONARY",
    "LIGHT_ACTIVE",
    "ACTIVE",
};
static_assert(sizeof(kStateNames) / sizeof(kStateNames[0]) ==
                  static_cast<size_t>(Fsm::State::COUNT),
              "kStateNames is out of sync with Fsm::State");

} // namespace

Fsm::Fsm(IFeatureSource& features, ISensorControl& sensors)
    : mFeatures(features)
    , mSensors(sensors)
    , mState(State::IDLE)
    , mStateTimestamp(features.nowMs())
    , mStateInit(true)
    , mHRMeasurementStartTime(0)
{
}

void Fsm::reset()
{
    mState                  = State::IDLE;
    mStateTimestamp         = mFeatures.nowMs();
    mStateInit              = true;
    mHRMeasurementStartTime = 0;
}

void Fsm::tick()
{
    // Run state handlers until the state stabilises. Cap iterations as
    // a safety net against any future logic that could ping-pong
    // between states inside a single tick.
    for (int i = 0; i < 8; ++i) {
        const State newState =
            (this->*kStateTable[static_cast<size_t>(mState)])();

        mStateInit = false;

        if (newState == mState) {
            return;
        }

        LOG_INFO("State transition: %s -> %s\n",
                 kStateNames[static_cast<size_t>(mState)],
                 kStateNames[static_cast<size_t>(newState)]);

        mState          = newState;
        mStateTimestamp = mFeatures.nowMs();
        mStateInit      = true;
    }

    LOG_WARNING("Tick reached transition cap; state=%s\n",
                kStateNames[static_cast<size_t>(mState)]);
}

bool Fsm::isTimeInState(uint32_t sec) const
{
    return (mFeatures.nowMs() - mStateTimestamp) > (sec * 1000);
}

Fsm::State Fsm::stateIdle()
{
    if (mStateInit) {
        LOG_DEBUG("Enter state IDLE\n");

        mSensors.disconnect(FsmSensor::PEDO);
        mSensors.disconnect(FsmSensor::FLOOR);
        mSensors.disconnect(FsmSensor::HR);
        mSensors.disconnect(FsmSensor::MOTION);
        mSensors.disconnect(FsmSensor::TOUCH);
    }

    return State::UNWORN;
}

Fsm::State Fsm::stateUnworn()
{
    if (mStateInit) {
        LOG_DEBUG("Enter state UNWORN\n");

        // The watch-on-wrist is detected by Touch + Motion. Steps
        // require an actual contact, so the pedometer can stay off
        // the whole time the watch is unworn.
        if (!mSensors.isConnected(FsmSensor::TOUCH)) {
            mSensors.connect(FsmSensor::TOUCH);
        }
        if (!mSensors.isConnected(FsmSensor::MOTION)) {
            mSensors.connect(FsmSensor::MOTION);
        }

        mSensors.disconnect(FsmSensor::PEDO);
        mSensors.disconnect(FsmSensor::HR);
        mSensors.disconnect(FsmSensor::FLOOR);
    }

    // Touch + Motion - Pedo is off here, so steps cannot serve as an
    // alternative wake-up signal.
    if (mFeatures.touchRaw() && mFeatures.motionRaw()) {
        LOG_DEBUG("Exit state UNWORN. Touch + Motion detected\n");
        return State::STATIONARY;
    }

    return State::UNWORN;
}

Fsm::State Fsm::stateStationary()
{
    const uint32_t steps = mFeatures.stepsLastMinute();

    if (mStateInit) {
        LOG_DEBUG("Enter state STATIONARY\n");

        if (!mSensors.isConnected(FsmSensor::TOUCH)) {
            mSensors.connect(FsmSensor::TOUCH);
        }
        if (!mSensors.isConnected(FsmSensor::MOTION)) {
            mSensors.connect(FsmSensor::MOTION);
        }
        if (!mSensors.isConnected(FsmSensor::PEDO)) {
            mSensors.connect(FsmSensor::PEDO);
        }

        // If we are arriving from LIGHT_ACTIVE / ACTIVE the HR sensor
        // may already be running. Two cases:
        //  - We have a valid trailing-minute window already -> stop
        //    immediately, no point keeping PPG warm.
        //  - We do not -> close out the current burst now (the next
        //    one starts on schedule).
        if (mSensors.isConnected(FsmSensor::HR)) {
            mSensors.disconnect(FsmSensor::HR);
            mHRMeasurementStartTime = mFeatures.nowMs();
        } else {
            // No probe in flight - make sure the next tick triggers one
            // immediately on first run.
            mHRMeasurementStartTime = 0;
        }

        mSensors.disconnect(FsmSensor::FLOOR);
    }

    // No Touch
    if (!mFeatures.isActuallyTouched() && isTimeInState(60)) {
        LOG_DEBUG("Exit state STATIONARY. No touch\n");
        return State::UNWORN;
    }

    // Touch without confirmation
    // 15 minutes for 2-3 attempts to check HR
    if (mFeatures.touchRaw() &&
        mFeatures.isNoMotion(15 * 60) &&
        steps == 0 &&
        !mFeatures.isHrValidLastMinute())
    {
        LOG_DEBUG("Exit state STATIONARY. Touch without confirmation\n");
        return State::UNWORN;
    }

    // Some movement or steps detected
    if (mFeatures.touchRaw() && (mFeatures.isMotion(30) || steps > 0)) {
        LOG_DEBUG("Exit state STATIONARY. Touch+ Motion detected %u, steps %u\n",
                  mFeatures.motionRaw(), steps);
        return State::LIGHT_ACTIVE;
    }

    tickHrProbe(skHrPeriodStationaryMs);

    return State::STATIONARY;
}

Fsm::State Fsm::stateLightActive()
{
    const uint32_t steps = mFeatures.stepsLastMinute();

    if (mStateInit) {
        LOG_DEBUG("Enter state LIGHT_ACTIVE\n");

        if (!mSensors.isConnected(FsmSensor::TOUCH)) {
            mSensors.connect(FsmSensor::TOUCH);
        }
        if (!mSensors.isConnected(FsmSensor::MOTION)) {
            mSensors.connect(FsmSensor::MOTION);
        }
        if (!mSensors.isConnected(FsmSensor::PEDO)) {
            mSensors.connect(FsmSensor::PEDO);
        }

        // HR is sampled in bursts here, not held continuously - see
        // tickHrProbe() below. If HR is already running (we just came
        // from ACTIVE) close out that burst on entry; the period
        // starts fresh from now.
        if (mSensors.isConnected(FsmSensor::HR)) {
            mSensors.disconnect(FsmSensor::HR);
            mHRMeasurementStartTime = mFeatures.nowMs();
        } else {
            mHRMeasurementStartTime = 0;
        }

        mSensors.disconnect(FsmSensor::FLOOR);
    }

    // No Touch
    if (!mFeatures.isActuallyTouched() && isTimeInState(60)) {
        LOG_DEBUG("Exit state LIGHT_ACTIVE. No touch\n");
        return State::UNWORN;
    }

    // Touch without confirmation - time-based heuristic
    if (mFeatures.touchRaw() &&
        mFeatures.isNoMotion(180) &&
        steps == 0 &&
        !mFeatures.isHrValidLastMinute())
    {
        LOG_DEBUG("Exit state LIGHT_ACTIVE. Touch without confirmation\n");
        return State::UNWORN;
    }

    // Touch without motion
    if (mFeatures.touchRaw() &&
        mFeatures.isNoMotion(180) &&
        steps == 0 &&
        mFeatures.isHrValidLastMinute())
    {
        LOG_DEBUG("Exit state LIGHT_ACTIVE. Touch without motion\n");
        return State::STATIONARY;
    }

    // Steps detected
    if (mFeatures.touchRaw() && steps >= 20) {
        LOG_DEBUG("Exit state LIGHT_ACTIVE. Touch+ steps %u\n", steps);
        return State::ACTIVE;
    }

    tickHrProbe(skHrPeriodLightActiveMs);

    return State::LIGHT_ACTIVE;
}

Fsm::State Fsm::stateActive()
{
    const uint32_t steps = mFeatures.stepsLastMinute();

    if (mStateInit) {
        LOG_DEBUG("Enter state ACTIVE\n");

        if (!mSensors.isConnected(FsmSensor::TOUCH)) {
            mSensors.connect(FsmSensor::TOUCH);
        }

        if (!mSensors.isConnected(FsmSensor::MOTION)) {
            mSensors.connect(FsmSensor::MOTION);
        }

        if (!mSensors.isConnected(FsmSensor::PEDO)) {
            mSensors.connect(FsmSensor::PEDO);
        }

        if (!mSensors.isConnected(FsmSensor::HR)) {
            mSensors.connect(FsmSensor::HR);
        }

        if (!mSensors.isConnected(FsmSensor::FLOOR)) {
            mSensors.connect(FsmSensor::FLOOR);
        }
    }

    // No Touch
    if (!mFeatures.isActuallyTouched() && isTimeInState(60)) {
        LOG_DEBUG("Exit state ACTIVE. No touch\n");
        return State::UNWORN;
    }

    // Touch without confirmation - time-based heuristic
    if (mFeatures.touchRaw() &&
        mFeatures.isNoMotion(180) &&
        steps == 0 &&
        !mFeatures.isHrValidLastMinute())
    {
        LOG_DEBUG("Exit state ACTIVE. Touch without confirmation\n");
        return State::UNWORN;
    }

    // Slowed down or stopped walking. Includes steps == 0 - a user
    // who just stops moving must be able to leave ACTIVE without
    // waiting for the no-touch / no-motion timeouts.
    if (mFeatures.touchRaw() &&
        steps < 20 &&
        isTimeInState(120))
    {
        LOG_DEBUG("Exit state ACTIVE. Touch+ steps %u\n", steps);
        return State::LIGHT_ACTIVE;
    }

    return State::ACTIVE;
}

void Fsm::tickHrProbe(uint32_t periodMs)
{
    const uint32_t now = mFeatures.nowMs();

    if (!mSensors.isConnected(FsmSensor::HR)) {
        // Off-cycle: open a new probe when the burst period elapses,
        // or on the very first run after entry (mHRMeasurementStartTime
        // is left at 0 by the state-init code in that case).
        const bool firstRun     = (mHRMeasurementStartTime == 0);
        const bool periodElapsed = (now - mHRMeasurementStartTime) > periodMs;
        if (firstRun || periodElapsed) {
            LOG_DEBUG("HR probe: start\n");
            mSensors.connect(FsmSensor::HR);
            mHRMeasurementStartTime = now;
        }
        return;
    }

    // On-cycle: end the probe when the tracker reports a valid window
    // (early-exit) or when the hard cap is reached, whichever comes
    // first. The minimum-duration floor lets the PPG AGC settle even
    // if validity reads true very early - typical for shared sensors
    // already running for someone else.
    const uint32_t elapsed = now - mHRMeasurementStartTime;
    const bool reachedMin  = elapsed >= skHrProbeMinMs;
    const bool reachedMax  = elapsed >= skHrProbeMaxMs;

    if (reachedMax || (reachedMin && mFeatures.isHrValidLastMinute())) {
        LOG_DEBUG("HR probe: stop (elapsed %u ms, valid %u)\n",
                  elapsed, mFeatures.isHrValidLastMinute());
        mSensors.disconnect(FsmSensor::HR);
    }
}

} // namespace Activity
