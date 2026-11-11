/**
 ******************************************************************************
 * @file    HrSourceArbiter.cpp
 * @brief   Selects the live heart-rate source (optical vs external strap).
 ******************************************************************************
 *
 * Pure selection policy — no BlueNRG/OS deps — so it is host-unit-tested.
 *
 ******************************************************************************
 */

#include "SensorLayer/Sensors/Body/HrSourceArbiter.hpp"

namespace Sensor
{

void HrSourceArbiter::submitOptical(uint8_t bpm, uint8_t trust, uint32_t nowMs)
{
    mOptical = Sample { true, bpm, trust, nowMs };
}

void HrSourceArbiter::submitExternal(uint8_t bpm, uint8_t trust, uint32_t nowMs)
{
    mExternal = Sample { true, bpm, trust, nowMs };
}

void HrSourceArbiter::reset()
{
    mOptical = Sample {};
    mExternal = Sample {};
    mSelected = HrSource::None;
    mLastSwitchMs = 0;
}

void HrSourceArbiter::clearExternal()
{
    mExternal = Sample {};                  // forget the last strap reading
    if (mSelected == HrSource::External) {
        mSelected = HrSource::None;         // re-decide; don't prefer the gone strap
    }
}

HrSourceArbiter::Output HrSourceArbiter::decide(uint32_t nowMs)
{
    // External must be fresh AND a real reading (ProfileHrs already drops 0).
    // Optical is taken whenever fresh — even a 0/low-trust value flows through,
    // matching the pre-arbiter behaviour where downstream thresholds decide.
    const bool extOk = fresh(mExternal, nowMs) && mExternal.bpm > 0;
    // External-only mode never selects optical: when the strap has no fresh
    // reading the output is invalid rather than falling back to the wrist.
    const bool optOk = !mExternalOnly && fresh(mOptical, nowMs);

    // Best available, external preferred.
    const HrSource candidate =
            extOk ? HrSource::External : (optOk ? HrSource::Optical
                                                : HrSource::None);

    const bool currentStillOk =
            (mSelected == HrSource::External && extOk) ||
            (mSelected == HrSource::Optical && optOk);

    HrSource next = mSelected;
    if (mSelected == HrSource::None) {
        // First acquisition is immediate.
        next = candidate;
    } else if (!currentStillOk) {
        // The selected source went stale: abandon it at once (do not hold a
        // dead source through the debounce window).
        next = candidate;
    } else if (candidate != mSelected) {
        // Both viable but a preferred source is available: switch only after
        // the anti-flap dwell has elapsed.
        if (nowMs - mLastSwitchMs >= kDebounceMs) {
            next = candidate;
        }
    }

    if (next != mSelected) {
        mSelected = next;
        mLastSwitchMs = nowMs;
    }

    Output out;
    out.source = mSelected;
    switch (mSelected) {
        case HrSource::External:
            out.valid = true;
            out.bpm = mExternal.bpm;
            out.trust = mExternal.trust;
            break;
        case HrSource::Optical:
            out.valid = true;
            out.bpm = mOptical.bpm;
            out.trust = mOptical.trust;
            break;
        case HrSource::None:
            out.valid = false;
            break;
    }
    return out;
}

HrSourceArbiter::ExOutput HrSourceArbiter::decideEx(uint32_t nowMs)
{
    ExOutput ex;
    ex.arbitrated = decide(nowMs);   // runs the arbitration (updates selection)

    // Raw per-source readings, independent of the arbitration policy (e.g. the
    // optical reading is reported even in external-only mode, when present), so
    // each source can be logged on its own. Same freshness rule as decide():
    // external must also be a real (non-zero) reading.
    const bool oFresh = fresh(mOptical, nowMs);
    ex.optical = { oFresh, oFresh ? mOptical.bpm : uint8_t(0),
                           oFresh ? mOptical.trust : uint8_t(0) };

    const bool eFresh = fresh(mExternal, nowMs) && mExternal.bpm > 0;
    ex.external = { eFresh, eFresh ? mExternal.bpm : uint8_t(0),
                            eFresh ? mExternal.trust : uint8_t(0) };

    return ex;
}

} /* namespace Sensor */
