/**
 ******************************************************************************
 * @file    ChargeThermalMonitor_test.cpp
 * @brief   Host tests for the charge-time / watch over-temperature logic.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "ChargeThermalMonitor.hpp"

namespace {

// Records the side effects the monitor drives so tests can assert on the final
// charge / warning / shutdown state.
class ActionRecorder : public IThermalActions
{
public:
    bool inhibitCharging() override
    {
        ++inhibitCount;
        if (failCharge) {
            return false;           // the PMIC write did not stick
        }
        charging = false;
        return true;
    }
    bool armCharging() override
    {
        ++armCount;
        if (failCharge) {
            return false;
        }
        charging = true;
        return true;
    }
    void raiseWarning(SystemError e) override { raised = e; hasRaised = true; }
    void clearWarning(SystemError e) override
    {
        if (hasRaised && raised == e) {
            hasRaised = false;
        }
    }
    void requestShutdown() override { ++shutdownCount; }
    void cancelShutdown() override  { ++cancelCount; }

    /// When set, both charge actions report failure, as a failed I2C
    /// read-modify-write to the PMIC would.
    bool        failCharge    = false;
    bool        charging      = true;   // armed at boot
    int         inhibitCount  = 0;
    int         armCount      = 0;
    bool        hasRaised     = false;
    SystemError raised        = SystemError::NONE;
    int         shutdownCount = 0;
    int         cancelCount   = 0;
};

// One evaluation tick: cache ambient, then feed the die (which drives eval).
void tick(ChargeThermalMonitor &m, float die, float ambient, uint32_t n = 1)
{
    for (uint32_t i = 0; i < n; ++i) {
        m.feedAmbient(ambient);
        m.feedDie(die);
    }
}

// Die-only tick (no fresh ambient), e.g. after USB has gone away.
void tickDie(ChargeThermalMonitor &m, float die, uint32_t n = 1)
{
    for (uint32_t i = 0; i < n; ++i) {
        m.feedDie(die);
    }
}

} // namespace

TEST(ChargeThermalMonitor, AmbientRuleTripsOnThirdReading)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, /*die*/46.0f, /*ambient*/47.0f, 2);   // 2 readings: not yet
    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(m.isLatched());

    tick(m, 46.0f, 47.0f, 1);                      // 3rd reading -> trip
    EXPECT_FALSE(rec.charging);
    EXPECT_TRUE(rec.hasRaised);
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_AMBIENT);
    EXPECT_TRUE(m.isLatched());
    EXPECT_EQ(rec.inhibitCount, 1);
}

TEST(ChargeThermalMonitor, NonConsecutiveReadingsDoNotTrip)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 30.0f, 47.0f, 2);   // 2 hot
    tick(m, 30.0f, 30.0f, 1);   // one cool reading resets the count
    tick(m, 30.0f, 47.0f, 2);   // 2 hot again - still short of 3 in a row

    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(m.isLatched());
}

TEST(ChargeThermalMonitor, CableRuleTripsOnDieAmbientDelta)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    // Ambient calm, die 15 C hotter, die still under the shutdown threshold.
    tick(m, /*die*/35.0f, /*ambient*/20.0f, 3);

    EXPECT_FALSE(rec.charging);
    EXPECT_TRUE(rec.hasRaised);
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_CABLE);
    EXPECT_EQ(rec.shutdownCount, 0);
}

TEST(ChargeThermalMonitor, NoChargingTripWhileUsbAbsent)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);

    // USB never asserted: warm die (below shutdown) must not stop charging.
    tickDie(m, 50.0f, 6);

    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(rec.hasRaised);
    EXPECT_FALSE(m.isLatched());
    EXPECT_EQ(rec.shutdownCount, 0);
}

TEST(ChargeThermalMonitor, RecoversAfterDieCoolsForThreeReadings)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 50.0f, 38.0f, 3);          // cable trip: die hot (>cool, <shutdown)
    ASSERT_TRUE(m.isLatched());
    ASSERT_FALSE(rec.charging);

    tick(m, 38.0f, 38.0f, 2);          // cool, but not for 3 readings yet
    EXPECT_TRUE(m.isLatched());
    EXPECT_FALSE(rec.charging);

    tick(m, 38.0f, 38.0f, 1);          // 3rd cool reading -> recover
    EXPECT_FALSE(m.isLatched());
    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(rec.hasRaised);
    EXPECT_EQ(rec.armCount, 1);
}

TEST(ChargeThermalMonitor, RecoveryProceedsAfterUsbLostWhileHot)
{
    // STUSB-fault case: tripped, then VBUS drops but the hot connector stays
    // attached. Ambient goes stale; the die keeps being fed and drives recovery.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 45.0f, 20.0f, 3);          // cable trip (die 45 < 55, no shutdown)
    ASSERT_TRUE(m.isLatched());

    m.setUsbPresent(false);            // VBUS drops, cable still in

    tickDie(m, 50.0f, 3);              // still hot -> stays latched
    EXPECT_TRUE(m.isLatched());
    EXPECT_FALSE(rec.charging);

    tickDie(m, 38.0f, 3);              // die cools -> recovers without USB return
    EXPECT_FALSE(m.isLatched());
    EXPECT_TRUE(rec.charging);
}

TEST(ChargeThermalMonitor, Rule3WithUsbWarnsCriticalAndStopsChargingNoShutdown)
{
    // die>55 but USB present: can't power off (VBUS keeps the watch on), so
    // warn to disconnect and stop charging instead.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 20.0f, 2);          // 2 hot readings: not yet
    EXPECT_FALSE(m.isLatched());

    tick(m, 56.0f, 20.0f, 1);          // 3rd -> critical warning, no shutdown
    EXPECT_EQ(rec.shutdownCount, 0);
    EXPECT_FALSE(m.isShutdownRequested());
    EXPECT_TRUE(rec.hasRaised);
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_CRITICAL);
    EXPECT_FALSE(rec.charging);
    EXPECT_TRUE(m.isLatched());
}

TEST(ChargeThermalMonitor, Rule2EscalatesToRule3CriticalWhileCharging)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 50.0f, 38.0f, 3);          // cable trip (delta 12, die<55)
    ASSERT_EQ(rec.raised, SystemError::TEMPERATURE_CABLE);

    tick(m, 56.0f, 38.0f, 3);          // die crosses 55 -> escalate
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_CRITICAL);
    EXPECT_EQ(rec.shutdownCount, 0);   // still no power-off while on USB
    EXPECT_FALSE(rec.charging);
}

TEST(ChargeThermalMonitor, UnplugWhileCriticalHotTriggersShutdown)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 38.0f, 3);          // critical, USB present -> no shutdown
    ASSERT_EQ(rec.shutdownCount, 0);

    m.setUsbPresent(false);            // cable pulled while still hot
    tickDie(m, 56.0f, 1);              // next tick -> now power off
    EXPECT_EQ(rec.shutdownCount, 1);
    EXPECT_TRUE(m.isShutdownRequested());
}

TEST(ChargeThermalMonitor, Rule3FiresWithoutUsb)
{
    // Sun / water ingress: no charging, whole watch overheats -> power off.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);

    tickDie(m, 60.0f, 3);
    EXPECT_EQ(rec.shutdownCount, 1);
    EXPECT_TRUE(m.isShutdownRequested());
}

TEST(ChargeThermalMonitor, Rule3NonConsecutiveDoesNotShutDown)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);

    tickDie(m, 56.0f, 2);
    tickDie(m, 50.0f, 1);              // dips below -> count resets
    tickDie(m, 56.0f, 2);
    EXPECT_EQ(rec.shutdownCount, 0);
}

TEST(ChargeThermalMonitor, ShutdownIsTerminalAndRequestedOnce)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);       // no USB -> power-off path

    tickDie(m, 60.0f, 5);              // well past the 3-reading trip
    EXPECT_EQ(rec.shutdownCount, 1);   // never re-requested
}

TEST(ChargeThermalMonitor, CriticalRecoversWhenDieCools)
{
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 38.0f, 3);          // critical, charging inhibited
    ASSERT_TRUE(m.isLatched());
    ASSERT_FALSE(rec.charging);

    tick(m, 38.0f, 38.0f, 3);          // die<50 + delta<7 for 3 -> recover
    EXPECT_FALSE(m.isLatched());
    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(rec.hasRaised);
}

TEST(ChargeThermalMonitor, PeakHoldNoStepDownOnCooldown)
{
    // Peak-hold: after CRITICAL, cooling must NOT step down to a lower warning
    // (cable/ambient) - the screen leaves, but charging stays inhibited.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 40.0f, 3);          // hot + cable -> CRITICAL (hot wins)
    ASSERT_EQ(rec.raised, SystemError::TEMPERATURE_CRITICAL);

    tick(m, 49.0f, 40.0f, 3);          // die<50 clears hot; cable still tripped
    EXPECT_FALSE(rec.hasRaised);       // peak-hold: no step-down warning
    EXPECT_TRUE(m.isLatched());        // but charging stays inhibited
    EXPECT_FALSE(rec.charging);

    tick(m, 45.0f, 40.0f, 3);          // delta 5<7 clears cable -> full recovery
    EXPECT_FALSE(m.isLatched());
    EXPECT_TRUE(rec.charging);
    EXPECT_FALSE(rec.hasRaised);
}

TEST(ChargeThermalMonitor, UnplugAfterHotClearedRecoversNoShutdown)
{
    // die>55 cooled below 50 (hot cleared, peak-hold hid the screen) but cable
    // still latched. Unplugging now must NOT power off (hot is already gone) -
    // it recovers via the die-fallback instead.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 40.0f, 3);          // CRITICAL (hot + cable)
    tick(m, 49.0f, 40.0f, 3);          // die<50 clears hot; cable still tripped
    ASSERT_TRUE(m.isLatched());
    ASSERT_FALSE(rec.hasRaised);       // peak-hold: nothing shown

    m.setUsbPresent(false);            // unplug after hot already cleared
    tickDie(m, 48.0f, 3);              // die<50 fallback clears cable -> recover
    EXPECT_EQ(rec.shutdownCount, 0);   // no power-off - hot was already gone
    EXPECT_FALSE(m.isLatched());
    EXPECT_TRUE(rec.charging);
}

TEST(ChargeThermalMonitor, ReEscalationAfterCooldownShowsAgain)
{
    // Peak-hold must not block a genuine re-escalation to the peak level.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 40.0f, 3);          // CRITICAL, peak = critical
    tick(m, 49.0f, 40.0f, 3);          // cool below hot -> screen leaves (peak-hold)
    ASSERT_FALSE(rec.hasRaised);

    tick(m, 56.0f, 40.0f, 3);          // heats back over 55 -> CRITICAL re-shows
    EXPECT_TRUE(rec.hasRaised);
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_CRITICAL);
}

TEST(ChargeThermalMonitor, HotReleasesBelow50SoUnplugCooledDoesNotShutDown)
{
    // ② the hot latch clears at die<50, so unplugging a cooled (but once-hot)
    // watch de-escalates instead of powering off.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);
    m.setUsbPresent(true);

    tick(m, 56.0f, 44.0f, 3);          // critical
    tick(m, 48.0f, 44.0f, 3);          // die<50 + delta<7 -> all clear, armed
    ASSERT_FALSE(m.isLatched());

    m.setUsbPresent(false);
    tickDie(m, 48.0f, 3);              // cooled + no USB -> no power-off
    EXPECT_EQ(rec.shutdownCount, 0);
    EXPECT_FALSE(m.isShutdownRequested());
}

TEST(ChargeThermalMonitor, ShutdownAbortsWhenUsbReconnects)
{
    // Variant A: USB comes back during the pre-power-off window -> cancel and
    // fall back to the critical warning instead of powering off.
    ActionRecorder rec;
    ChargeThermalMonitor m(rec);       // no USB

    tickDie(m, 60.0f, 3);              // hot, no USB -> shutdown requested
    ASSERT_EQ(rec.shutdownCount, 1);
    ASSERT_TRUE(m.isShutdownRequested());

    m.setUsbPresent(true);             // reconnect before power-off ran
    EXPECT_EQ(rec.cancelCount, 1);
    EXPECT_FALSE(m.isShutdownRequested());

    tick(m, 60.0f, 20.0f, 1);          // now hot + USB -> critical, not shutdown
    EXPECT_EQ(rec.shutdownCount, 1);   // no new power-off
    EXPECT_EQ(rec.raised, SystemError::TEMPERATURE_CRITICAL);
    EXPECT_FALSE(rec.charging);
}

// The charge-disable write goes over I2C and can fail. The monitor must not
// treat "asked" as "done": charging would keep running for the whole episode
// while the watch, the screen and the phone all reported it stopped, and with
// a cable attached Rule 3 has no power-off to fall back on.
TEST(ChargeThermalMonitor, RetriesInhibitUntilTheHardwareTakesIt)
{
    ActionRecorder a;
    ChargeThermalMonitor m(a);
    m.setUsbPresent(true);

    a.failCharge = true;
    tick(m, /*die*/50.0f, /*ambient*/38.0f, 3);   // delta 12 -> cable rule trips

    EXPECT_TRUE(a.hasRaised);                 // the user is still warned
    EXPECT_TRUE(a.charging);                  // but the write did not stick
    EXPECT_FALSE(m.isLatched());              // so nothing is assumed
    const int attemptsSoFar = a.inhibitCount;
    EXPECT_GE(attemptsSoFar, 1);

    tick(m, 50.0f, 38.0f, 2);                 // still tripped -> keeps trying
    EXPECT_GT(a.inhibitCount, attemptsSoFar);
    EXPECT_TRUE(a.charging);

    a.failCharge = false;                     // bus recovers
    tick(m, 50.0f, 38.0f);
    EXPECT_FALSE(a.charging);                 // charging really stops now
    EXPECT_TRUE(m.isLatched());

    const int settled = a.inhibitCount;
    tick(m, 50.0f, 38.0f, 3);                 // and it is not re-asserted after
    EXPECT_EQ(a.inhibitCount, settled);
}

TEST(ChargeThermalMonitor, RetriesReArmUntilTheHardwareTakesIt)
{
    ActionRecorder a;
    ChargeThermalMonitor m(a);
    m.setUsbPresent(true);

    tick(m, 50.0f, 38.0f, 3);                 // trip and inhibit for real
    ASSERT_TRUE(m.isLatched());
    ASSERT_FALSE(a.charging);

    a.failCharge = true;
    tick(m, 40.0f, 38.0f, 3);                 // cools: delta 2 -> clears
    EXPECT_TRUE(m.isLatched());               // re-arm failed, stay latched
    EXPECT_FALSE(a.charging);                 // not charging: the safe direction

    a.failCharge = false;
    tick(m, 40.0f, 38.0f);
    EXPECT_TRUE(a.charging);
    EXPECT_FALSE(m.isLatched());
}
