/**
 ******************************************************************************
 * @file    ChargeThermalMonitor.cpp
 ******************************************************************************
 */

#include "ChargeThermalMonitor.hpp"

ChargeThermalMonitor::ChargeThermalMonitor(IThermalActions &actions)
    : mActions(actions)
{
}

void ChargeThermalMonitor::setUsbPresent(bool present)
{
    if (mUsbPresent == present) {
        return;
    }
    mUsbPresent = present;

    if (present) {
        // USB came back. If a power-off was pending it has not run yet (it is a
        // raw PMIC mode change with no controlled teardown before it), so
        // cancelling is safe; the next tick reconciles back to the critical
        // warning while USB keeps the watch powered.
        if (mShutdownRequested) {
            mShutdownRequested = false;
            mHasShown = false;
            mShown    = SystemError::NONE;
            mActions.cancelShutdown();
        }
    } else {
        // The baro is unsubscribed on USB loss, so ambient goes stale and the
        // charging rules stop tripping. Their clear then falls back to the die
        // (see evaluate()); reset their in-flight debounce.
        mHaveAmbient       = false;
        mAmbientCount      = 0;
        mCableCount        = 0;
        mAmbientClearCount = 0;
        mCableClearCount   = 0;
    }
}

void ChargeThermalMonitor::feedAmbient(float degC)
{
    mAmbient     = degC;
    mHaveAmbient = true;
}

void ChargeThermalMonitor::feedDie(float degC)
{
    evaluate(degC);
}

void ChargeThermalMonitor::updateRule(bool &tripped, uint8_t &tripCount,
                                      uint8_t &clearCount,
                                      bool aboveTrip, bool belowClear)
{
    if (!tripped) {
        clearCount = 0;
        if (aboveTrip) {
            if (++tripCount >= skTripReadings) {
                tripped   = true;
                tripCount = 0;
            }
        } else {
            tripCount = 0;
        }
    } else {
        tripCount = 0;
        if (belowClear) {
            if (++clearCount >= skClearReadings) {
                tripped    = false;
                clearCount = 0;
            }
        } else {
            clearCount = 0;
        }
    }
}

void ChargeThermalMonitor::evaluate(float die)
{
    // Once a power-off is requested the watch is going down; stop evaluating.
    if (mShutdownRequested) {
        return;
    }

    // Rule 3 - critical die temperature. Detection is independent of charging.
    updateRule(mHotTripped, mHotCount, mHotClearCount,
               die > skShutdownC, die < skHotClearC);

    // With valid VBUS the PMIC keeps the watch powered, so a power-off would
    // hang; only when USB is absent can we actually shut down. While USB is
    // present the critical state falls through to reconcile(), which raises
    // TEMPERATURE_CRITICAL and stops charging.
    if (mHotTripped && !mUsbPresent) {
        mShutdownRequested = true;
        mActions.requestShutdown();
        return;
    }

    // Rules 1 & 2 - only trip while charging is possible (USB present) and the
    // ambient reading is live. They clear on ambient/delta hysteresis while it
    // is live, or on the die falling below kHotClearC once ambient is stale.
    const bool haveAmbient = mUsbPresent && mHaveAmbient;
    updateRule(mAmbientTripped, mAmbientCount, mAmbientClearCount,
               haveAmbient && (mAmbient > skAmbientTripC),
               haveAmbient ? (mAmbient < skAmbientClearC) : (die < skHotClearC));
    updateRule(mCableTripped, mCableCount, mCableClearCount,
               haveAmbient && ((die - mAmbient) > skDeltaTripC),
               haveAmbient ? ((die - mAmbient) < skDeltaClearC) : (die < skHotClearC));

    reconcile();
}

void ChargeThermalMonitor::reconcile()
{
    // Severity of the highest tripped rule: critical (Rule 3, USB present - the
    // USB-absent case powered off before reaching here) beats the cable
    // "disconnect now", which beats the ambient warning.
    uint8_t     wantRank = 0;
    SystemError wantCode = SystemError::NONE;
    if (mHotTripped) {
        wantRank = 3;
        wantCode = SystemError::TEMPERATURE_CRITICAL;
    } else if (mCableTripped) {
        wantRank = 2;
        wantCode = SystemError::TEMPERATURE_CABLE;
    } else if (mAmbientTripped) {
        wantRank = 1;
        wantCode = SystemError::TEMPERATURE_AMBIENT;
    }

    if (wantRank == 0) {
        // Full recovery - drop the warning, re-arm charging, reset the peak.
        if (mHasShown) {
            mActions.clearWarning(mShown);
            mHasShown = false;
            mShown    = SystemError::NONE;
        }
        if (mLatched) {
            // Stay latched if the re-arm did not take, so the next tick tries
            // again. Failing this way leaves the watch not charging, which is
            // the harmless direction.
            mLatched = !mActions.armCharging();
        }
        mShownPeak = 0;
        return;
    }

    // Something is tripped: charging stays inhibited regardless of the display.
    // Only latch once the hardware confirms it, otherwise a dropped write would
    // leave the watch charging for the whole episode while everything reported
    // it stopped. Unlatched means untrusted, so every tick re-asserts until it
    // sticks - and with a cable attached there is no power-off to fall back on.
    if (!mLatched) {
        mLatched = mActions.inhibitCharging();
    }

    if (wantRank >= mShownPeak) {
        // Escalation (or same level) - show/switch to it and raise the peak.
        if (!mHasShown || mShown != wantCode) {
            if (mHasShown) {
                mActions.clearWarning(mShown);
            }
            mActions.raiseWarning(wantCode);
            mShown    = wantCode;
            mHasShown = true;
        }
        mShownPeak = wantRank;
    } else {
        // De-escalation (cooling): peak-hold - do not step down to a lower
        // warning, that would re-alert for something the user was already told
        // about in a worse form. Drop the alert instead; the rule is still
        // tripped, so charging stays inhibited until it clears for real.
        if (mHasShown) {
            mActions.clearWarning(mShown);
            mHasShown = false;
            mShown    = SystemError::NONE;
        }
    }
}
