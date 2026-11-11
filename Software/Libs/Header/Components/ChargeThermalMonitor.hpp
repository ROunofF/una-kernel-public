/**
 ******************************************************************************
 * @file    ChargeThermalMonitor.hpp
 * @brief   Over-temperature detection for the charge path and the watch.
 *
 *          A bad cable or debris in the USB connector can heat the port while
 *          charging even when the USB protection has not tripped; sun or water
 *          ingress can overheat the whole watch. This watches two temperature
 *          sources - the fuel-gauge die (near the USB connector) and the
 *          altimeter ambient (opposite side of the case) - and acts when a rule
 *          holds for three consecutive readings:
 *
 *            Rule 1 (charging): ambient         >  kAmbientTripC   -> stop charging
 *            Rule 2 (charging): (die - ambient) >  kDeltaTripC     -> stop charging
 *            Rule 3 (always):   die             >  kShutdownC      -> see below
 *
 *          Rule 3's action depends on USB. With valid VBUS the PMIC keeps the
 *          watch powered (a power-off would not stick and hangs the shutdown
 *          path), so while USB is present Rule 3 can only stop charging and
 *          raise a "disconnect the cable" warning; without USB it powers off.
 *          If USB is (re)connected during the pre-power-off window the shutdown
 *          is cancelled and the state falls back to the critical warning (the
 *          power-off is a raw PMIC mode change, so cancelling before it runs is
 *          safe). Rule 2 -> Rule 3 is a warning escalation as the die crosses
 *          kShutdownC. Cooling does not walk the warning back down: the alert
 *          is held at the highest level reached, so a de-escalation drops it
 *          rather than nagging with a milder one. Charging stays inhibited
 *          until every rule has cleared, independently of what is shown.
 *
 *          The die reading is the evaluation tick: feedDie() is expected once
 *          per fuel-gauge poll (~5 s), and each call counts as one reading, so
 *          three readings is ~15 s. feedAmbient() only caches the latest value
 *          between ticks. The logic is count-based and carries no clock.
 *
 *          Each rule clears with its own hysteresis (ambient<kAmbientClearC,
 *          delta<kDeltaClearC, die<kHotClearC) held for three readings, so the
 *          warning de-escalates and charging re-arms as things cool - no single
 *          global cooldown threshold. Ambient goes stale when USB is unplugged
 *          (the baro is unsubscribed), so the ambient/cable rules then clear on
 *          the die falling below kHotClearC instead. The die is fed with or
 *          without USB, so recovery works whether the cable was unplugged or a
 *          fault dropped VBUS while the hot connector stayed attached. A
 *          disconnect that outlasts the clear window therefore ends the episode
 *          by itself, on a cool die: the rules clear while unplugged, and the
 *          next plug is evaluated from scratch rather than resuming the old
 *          state.
 *
 *          Pure logic: no OS, hardware, or sensor-layer dependencies. Side
 *          effects go through IThermalActions, so host tests drive it directly.
 ******************************************************************************
 */

#ifndef CHARGE_THERMAL_MONITOR_HPP
#define CHARGE_THERMAL_MONITOR_HPP

#include <cstdint>

#include "SystemError.hpp"

/**
 * @brief   Side effects the monitor drives. Implemented by the owner
 *          (ErrorMonitor), which routes them to Power and the error screen.
 */
class IThermalActions
{
public:
    /// Stop the charge path (reversible). Returns false if the hardware did
    /// not take it, so the caller can try again rather than assume it stuck -
    /// this is the safety action, and it goes over a bus that can fail.
    virtual bool inhibitCharging() = 0;

    /// Re-enable the charge path. Same contract as inhibitCharging().
    virtual bool armCharging() = 0;

    /// Raise a user-facing error (screen + buzzer).
    virtual void raiseWarning(SystemError e) = 0;

    /// Clear a previously raised error.
    virtual void clearWarning(SystemError e) = 0;

    /// Begin the over-temperature power-off sequence (warn + buzz, then off).
    virtual void requestShutdown() = 0;

    /// Abort a pending power-off (USB came back before it ran).
    virtual void cancelShutdown() = 0;

protected:
    virtual ~IThermalActions() = default;
};

class ChargeThermalMonitor
{
public:
    explicit ChargeThermalMonitor(IThermalActions &actions);

    /**
     * @brief   USB presence changed. Rules 1/2 only trip while USB is present;
     *          on loss the ambient reading is dropped (the baro is unsubscribed)
     *          and their trip/clear progress resets. If a power-off is pending,
     *          a reconnect cancels it. Rule 3 detection is independent of USB.
     */
    void setUsbPresent(bool present);

    /**
     * @brief   New altimeter ambient temperature. Cached for the next die tick;
     *          does not itself advance any rule.
     */
    void feedAmbient(float degC);

    /**
     * @brief   New fuel-gauge die temperature. This is the evaluation tick: one
     *          call per gauge poll (~5 s), so all rules advance by one reading.
     */
    void feedDie(float degC);

    // Introspection for tests.
    bool isLatched() const           { return mLatched; }
    bool isShutdownRequested() const { return mShutdownRequested; }

private:
    // Trip thresholds and their clear (hysteresis) counterparts. Tunable.
    static constexpr float   skAmbientTripC   = 45.0f;
    static constexpr float   skAmbientClearC  = 43.0f;
    static constexpr float   skDeltaTripC     = 10.0f;
    static constexpr float   skDeltaClearC    = 7.0f;
    static constexpr float   skShutdownC      = 55.0f;
    static constexpr float   skHotClearC      = 50.0f;
    static constexpr uint8_t skTripReadings   = 3;   // ~15 s at a 5 s cadence
    static constexpr uint8_t skClearReadings  = 3;

    /// One evaluation tick against @p die and the cached ambient.
    void evaluate(float die);

    /// Advance one rule's trip/clear debounce toward @p aboveTrip / @p belowClear.
    static void updateRule(bool &tripped, uint8_t &tripCount, uint8_t &clearCount,
                           bool aboveTrip, bool belowClear);

    /// Reconcile the displayed warning and charge state with the tripped rules:
    /// raise/switch to the highest-priority one, or clear + re-arm when none.
    void reconcile();

    IThermalActions &mActions;

    bool  mUsbPresent  = false;
    bool  mHaveAmbient = false;
    float mAmbient     = 0.0f;

    uint8_t mAmbientCount = 0;
    uint8_t mCableCount   = 0;
    uint8_t mHotCount     = 0;

    uint8_t mAmbientClearCount = 0;
    uint8_t mCableClearCount   = 0;
    uint8_t mHotClearCount     = 0;

    bool mAmbientTripped    = false;
    bool mCableTripped      = false;
    bool mHotTripped        = false;   // die>55 reached; action gated on USB
    bool mShutdownRequested = false;

    bool        mLatched  = false;
    bool        mHasShown = false;
    SystemError mShown    = SystemError::NONE;

    // Peak-hold: the highest warning severity shown this episode (1 ambient,
    // 2 cable, 3 critical). We alert on escalation only; on cooldown the screen
    // leaves rather than stepping down to a lower warning. Reset on full
    // recovery. Charging stays inhibited while any rule is tripped regardless.
    uint8_t mShownPeak = 0;
};

#endif // CHARGE_THERMAL_MONITOR_HPP
