/**
 ******************************************************************************
 * @file    SensorStubFloor.cpp
 * @brief   Floor counter (pressure-based)
 *
 ******************************************************************************
 *
 * Floor counting based on station pressure changes (Pa or consistent pressure units),
 * optionally gated by pedometer steps.
 *
 * Sign convention (dp = p - baseline):
 *  - Climbing (UP):    pressure decreases  -> dp < 0
 *  - Descending (DOWN): pressure increases -> dp > 0
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "FloorCounter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Floor/FloorCounter.hpp"

#include <cmath>

namespace Sensor {

FloorCounter::FloorCounter() : FloorCounter(Params{})
{
}

/**
 * @brief Construct a new FloorCounter object.
 *
 * Initializes parameters, internal filters and counters.
 *
 * @param p Algorithm parameters (floor height, thresholds, window size, EMA alpha).
 */
FloorCounter::FloorCounter(const Params& p)
    : mParams(p)
    , mPressure(p.alphaSignal)
    , mBaseLine(p.alphaBaseline)
    , mState(State::Idle)
    , mSteps()
    , mFloorsUp(0)
    , mFloorsDown(0)
    , mMovementTimer(SW_TIMER_SECONDS(20)) //
    , mLandingDetector(LandingDetector::Config{
                            .derivativeThreshold = 1.2f,
                            .confirmInterval     = 2000,
                            .derivativeLpfAlpha  = 0.9f
      })
{
    setCounters(0, 0);
}

void FloorCounter::resetFSM()
{
    mState = State::Idle;

    mSteps.clear();

    mPressure.reset();
    mBaseLine.reset();
}

/**
 * @brief Reset state machine, counters and baseline.
 *
 * Sets state to Idle, clears step history, resets pressure filter
 * and clears baseline.
 */
void FloorCounter::setCounters(uint32_t up, uint32_t down)
{
    mFloorsUp   = up;
    mFloorsDown = down;

    resetFSM();
}

/**
 * @brief Feed a new step event.
 *
 * Adds the step timestamp to the sliding window and updates the window.
 *
 * @param ts Step timestamp [ms].
 */
void FloorCounter::newStep(uint32_t ts)
{
    mSteps.push(ts);
    updateStepWindow(ts);
}

float FloorCounter::getBaseline()
{
    return mBaseLine.getValue();
}

float FloorCounter::getDelta()
{
    return (mPressure.getValue() - mBaseLine.getValue());
}

/**
 * @brief Feed a new station pressure sample.
 *
 * - Smooths the input pressure with EMA.
 * - Initializes baseline on the first valid sample.
 * - Executes the finite-state machine to check transitions.
 *
 * @param pressure Station pressure sample [Pa] (or consistent pressure units).
 * @param ts       Timestamp [ms].
 */
void FloorCounter::newPressure(float pressure, uint32_t ts)
{
    mPressure.execute(pressure);

    if (mBaseLine.isReseted()) {
        mBaseLine.forceValue(mPressure.getValue());
    } else if (mLandingDetector.process(pressure, ts)) {
        mBaseLine.forceValue(mLandingDetector.landingLevel());
    }

    LOG_DEBUG("p=%.2f pf=%.2f base=%.2f dp=%.2f thldB=%.2f thldE=%.2f STATE=%u\n", pressure,
                                                                                   mPressure.getValue(),
                                                                                   mBaseLine.getValue(),
                                                                                   mPressure.getValue() - mBaseLine.getValue(),
                                                                                   Params::thldStart,
                                                                                   Params::thldConfirm,
                                                                                   static_cast<uint32_t>(mState));

    LOG_DEBUG("derivate=%.3f\n", mLandingDetector.getDerivate());

    executeFSM(ts);
}

/**
 * @brief Remove outdated steps from sliding window.
 *
 * @param ts Current timestamp [ms].
 */
void FloorCounter::updateStepWindow(uint32_t ts)
{
    const uint32_t tmin = ts - mParams.windowMs;
    while (!mSteps.empty() && mSteps.front() < tmin) {
        mSteps.pop();
    }
}

/**
 * @brief Check if enough steps occurred within the configured time window.
 *
 * @param ts Current timestamp [ms].
 * @return true if step count >= minStepsWindow, false otherwise.
 */
bool FloorCounter::stepGateOK(uint32_t ts) const
{
    const uint32_t tmin = ts - mParams.windowMs;

    uint32_t count = 0;
    for (auto t : mSteps) {
        if (t >= tmin) {
            ++count;
        }
    }

    LOG_DEBUG("stepGate(%u): count=%u/%u window=%ums\n",
                                                         static_cast<unsigned>(mSteps.size()),
                                                         count,
                                                         mParams.minStepsWindow,
                                                         mParams.windowMs);

    return count >= mParams.minStepsWindow;
}

/**
 * @brief Executes the pressure-based finite-state machine for floor counting.
 *
 * The FSM uses the filtered pressure signal relative to a slowly tracked baseline
 * to detect ascent and descent events. A valid movement is only considered when
 * the step gate is open, which helps reject elevator and escalator motion.
 *
 * Pressure thresholds:
 *  - thldStart     = startThreshRatio   * floorPressure
 *  - thldConfirm   = confirmThreshRatio * floorPressure
 *  - thldRemaining = (1.0f - confirmThreshRatio) * floorPressure
 *
 * where:
 *  - floorPressure = floorHeight * paPerMeter
 *  - dp            = p - baseline
 *
 * Sign convention:
 *  - ascent  -> pressure decreases -> dp becomes negative
 *  - descent -> pressure increases -> dp becomes positive
 *
 * States:
 * - Idle:
 *   - Baseline is continuously updated to follow slow pressure drift.
 *   - If steps are valid and (-dp) >= thldStart, transition to Climbing
 *     and start the timeout timer.
 *   - If steps are valid and dp >= thldStart, transition to Descending
 *     and start the timeout timer.
 *
 * - Climbing:
 *   - If the step gate closes or the timeout expires, force the current
 *     pressure as baseline and return to Idle.
 *   - If (-dp) >= thldConfirm, increment the upward floor counter,
 *     force the current pressure as baseline, and transition to
 *     ClimbingSettling.
 *   - If dp >= thldStart, treat this as a reversal, restart the timer,
 *     force the current pressure as baseline, and transition to Descending.
 *
 * - ClimbingSettling:
 *   - Used after an upward floor has been confirmed, to trace the remaining
 *     part of the pressure transition.
 *   - If the step gate closes or the timeout expires, force the current
 *     pressure as baseline and return to Idle.
 *   - If (-dp) >= thldRemaining, force the current pressure as baseline
 *     and return to Idle.
 *   - If dp >= thldStart, treat this as a reversal, restart the timer,
 *     force the current pressure as baseline, and transition to Descending.
 *
 * - Descending:
 *   - If the step gate closes or the timeout expires, force the current
 *     pressure as baseline and return to Idle.
 *   - If dp >= thldConfirm, increment the downward floor counter,
 *     force the current pressure as baseline, and transition to
 *     DescendingSettling.
 *   - If (-dp) >= thldStart, treat this as a reversal, restart the timer,
 *     force the current pressure as baseline, and transition to Climbing.
 *
 * - DescendingSettling:
 *   - Used after a downward floor has been confirmed, to trace the remaining
 *     part of the pressure transition.
 *   - If the step gate closes or the timeout expires, force the current
 *     pressure as baseline and return to Idle.
 *   - If dp >= thldRemaining, force the current pressure as baseline
 *     and return to Idle.
 *   - If (-dp) >= thldStart, treat this as a reversal, restart the timer,
 *     force the current pressure as baseline, and transition to Climbing.
 *
 * @param ts Current timestamp in milliseconds.
 */
void FloorCounter::executeFSM(uint32_t ts)
{
    const float p        = mPressure.getValue();
    const float baseline = mBaseLine.getValue();
    const float dp       = p - baseline;

    switch (mState) {
    case State::Idle:
    {
        mBaseLine.execute(p);

        if (stepGateOK(ts)) {
            if ((-dp) >= Params::thldStart) {
                mState = State::Climbing;
                mMovementTimer.start();
            } else if (dp >= Params::thldStart) {
                mState = State::Descending;
                mMovementTimer.start();
            }
        }
        break;
    }

    case State::Climbing:
    {
        if (!stepGateOK(ts) || mMovementTimer.expired()) {
            mBaseLine.forceValue(p);
            mState = State::Idle;
        } else {
            if ((-dp) >= Params::thldConfirm) {
                ++mFloorsUp;
                mBaseLine.forceValue(p);
                mState = State::ClimbingSettling;
            } else if (dp >= Params::thldStart) {
                mMovementTimer.start();
                mBaseLine.forceValue(p);
                mState = State::Descending;
            }
        }
        break;
    }

    case State::ClimbingSettling:
    {
        if (!stepGateOK(ts) || mMovementTimer.expired()) {
            mBaseLine.forceValue(p);
            mState = State::Idle;
        } else {
            if ((-dp) >= Params::thldRemaining) {
                mBaseLine.forceValue(p);
                mState = State::Idle;
            } else if (dp >= Params::thldStart) {
                mMovementTimer.start();
                mBaseLine.forceValue(p);
                mState = State::Descending;
            }
        }
        break;
    }

    case State::Descending:
    {
        if (!stepGateOK(ts) || mMovementTimer.expired()) {
            mBaseLine.forceValue(p);
            mState = State::Idle;
        } else {
            if (dp >= Params::thldConfirm) {
                ++mFloorsDown;
                mBaseLine.forceValue(p);
                mState = State::DescendingSettling;
            } else if ((-dp) >= Params::thldStart) {
                mMovementTimer.start();
                mBaseLine.forceValue(p);
                mState = State::Climbing;
            }
        }
        break;
    }

    case State::DescendingSettling:
    {
        if (!stepGateOK(ts) || mMovementTimer.expired()) {
            mBaseLine.forceValue(p);
            mState = State::Idle;
        } else {
            if (dp >= Params::thldRemaining) {
                mBaseLine.forceValue(p);
                mState = State::Idle;
            } else if ((-dp) >= Params::thldStart) {
                mMovementTimer.start();
                mBaseLine.forceValue(p);
                mState = State::Climbing;
            }
        }
        break;
    }
    }
}

} // namespace Sensor
