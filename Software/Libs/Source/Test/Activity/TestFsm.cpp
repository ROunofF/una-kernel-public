/**
 ******************************************************************************
 * @file    TestFsm.cpp
 * @brief   Unit tests for Activity::Fsm.
 *
 *          Pure-logic tests via mocked IFeatureSource / ISensorControl.
 *          Covers state transitions, per-state sensor maps, and the
 *          time-in-state thresholds. The HR bursty probe scheduler
 *          (tickHrProbe) is not exercised here - it depends on real-
 *          time cadence and is observed via the runtime sensor test.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestFsm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/Activity/TestFsm.hpp"
#include "Activity/Fsm.hpp"
#include "Activity/FsmInterfaces.hpp"
#include "Activity/Types.hpp"

namespace {

using Activity::Fsm;
using Activity::FsmSensor;

// --- Mocks -------------------------------------------------------------------

class MockFeatures : public Activity::IFeatureSource
{
public:
    void reset()
    {
        nowMs_val               = 0;
        stepsLastMinute_val     = 0;
        isHrValidLastMinute_val = false;
        isActuallyTouched_val   = false;
        motionContinuousSec     = 0;
        noMotionContinuousSec   = 0;
        touchRaw_val            = false;
        motionRaw_val           = false;
    }

    uint32_t nowMs() const override               { return nowMs_val; }
    uint32_t stepsLastMinute() const override     { return stepsLastMinute_val; }
    bool     isHrValidLastMinute() const override { return isHrValidLastMinute_val; }
    bool     isActuallyTouched() const override   { return isActuallyTouched_val; }
    bool     isMotion(uint32_t sec) const override
    {
        return motionContinuousSec >= sec;
    }
    bool     isNoMotion(uint32_t sec) const override
    {
        return noMotionContinuousSec >= sec;
    }
    bool     touchRaw() const override            { return touchRaw_val; }
    bool     motionRaw() const override           { return motionRaw_val; }

    uint32_t nowMs_val               = 0;
    uint32_t stepsLastMinute_val     = 0;
    bool     isHrValidLastMinute_val = false;
    bool     isActuallyTouched_val   = false;
    uint32_t motionContinuousSec     = 0;
    uint32_t noMotionContinuousSec   = 0;
    bool     touchRaw_val            = false;
    bool     motionRaw_val           = false;
};

class MockSensors : public Activity::ISensorControl
{
public:
    void reset()
    {
        for (size_t i = 0; i < kCount; ++i) {
            connected[i] = false;
        }
    }

    void connect(FsmSensor s) override
    {
        connected[static_cast<size_t>(s)] = true;
    }
    void disconnect(FsmSensor s) override
    {
        connected[static_cast<size_t>(s)] = false;
    }
    bool isConnected(FsmSensor s) override
    {
        return connected[static_cast<size_t>(s)];
    }

    bool isOn(FsmSensor s) const
    {
        return connected[static_cast<size_t>(s)];
    }

    static constexpr size_t kCount = static_cast<size_t>(FsmSensor::COUNT);
    bool connected[kCount] = {};
};

// --- Helpers -----------------------------------------------------------------

bool expectState(Fsm::State got, Fsm::State want, const char* scenario)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got state %d, want %d\n",
              scenario, (int)got, (int)want);
    return false;
}

bool expectSensor(MockSensors& s, FsmSensor sensor,
                  bool wantOn, const char* scenario)
{
    const bool gotOn = s.isOn(sensor);
    if (gotOn == wantOn) {
        return true;
    }
    LOG_ERROR("%s: sensor %d got %d, want %d\n",
              scenario, (int)sensor, (int)gotOn, (int)wantOn);
    return false;
}

// Drive the FSM into the named state. After the call: nothing else
// triggers, the FSM is stable in `target`.
void driveTo(Fsm& fsm, MockFeatures& f, MockSensors& s, Fsm::State target)
{
    f.reset();
    s.reset();
    fsm.reset();
    fsm.tick();             // IDLE -> UNWORN, stable
    if (target == Fsm::State::UNWORN) return;

    f.touchRaw_val  = true;
    f.motionRaw_val = true;
    fsm.tick();             // UNWORN -> STATIONARY
    if (target == Fsm::State::STATIONARY) return;

    // Make hr valid + non-zero steps so we cross into LIGHT_ACTIVE
    // without satisfying the fake-touch-to-UNWORN guard. Keep steps
    // below ACTIVE threshold (>=20) so we don't skip past.
    f.isHrValidLastMinute_val = true;
    f.stepsLastMinute_val     = 5;
    fsm.tick();             // STATIONARY -> LIGHT_ACTIVE
    if (target == Fsm::State::LIGHT_ACTIVE) return;

    f.stepsLastMinute_val = 25;
    fsm.tick();             // LIGHT_ACTIVE -> ACTIVE
}

// --- Tests -------------------------------------------------------------------

bool testInitialStateIsIdle()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    return expectState(fsm.current(), Fsm::State::IDLE, "initialState");
}

bool testResetReturnsToIdle()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);
    if (!expectState(fsm.current(), Fsm::State::STATIONARY, "preReset")) {
        return false;
    }
    fsm.reset();
    return expectState(fsm.current(), Fsm::State::IDLE, "afterReset");
}

bool testIdleEntryDisconnectsAll()
{
    MockFeatures f;
    MockSensors  s;
    // Pre-mark everything connected. IDLE entry should disconnect it.
    for (size_t i = 0; i < MockSensors::kCount; ++i) {
        s.connected[i] = true;
    }

    Fsm fsm(f, s);
    fsm.reset();
    fsm.tick();   // IDLE -> UNWORN within one tick

    // After tick: UNWORN entry has reconnected TOUCH + MOTION, others
    // remain disconnected from the IDLE-entry sweep.
    if (!expectSensor(s, FsmSensor::TOUCH,  true,  "idleDisc.touch"))  return false;
    if (!expectSensor(s, FsmSensor::MOTION, true,  "idleDisc.motion")) return false;
    if (!expectSensor(s, FsmSensor::PEDO,   false, "idleDisc.pedo"))   return false;
    if (!expectSensor(s, FsmSensor::HR,     false, "idleDisc.hr"))     return false;
    if (!expectSensor(s, FsmSensor::FLOOR,  false, "idleDisc.floor"))  return false;
    return expectState(fsm.current(), Fsm::State::UNWORN, "idleDisc.state");
}

bool testIdleToUnwornOnFirstTick()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    fsm.reset();
    fsm.tick();
    return expectState(fsm.current(), Fsm::State::UNWORN, "idle->unworn");
}

bool testUnwornStaysIdleWhenNoTouchNoMotion()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::UNWORN);
    fsm.tick();    // still no touch, no motion
    return expectState(fsm.current(), Fsm::State::UNWORN, "unworn.stay");
}

bool testUnwornToStationary()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::UNWORN);

    f.touchRaw_val  = true;
    f.motionRaw_val = true;
    fsm.tick();

    if (!expectState(fsm.current(), Fsm::State::STATIONARY, "u->s.state")) {
        return false;
    }
    return expectSensor(s, FsmSensor::PEDO, true, "u->s.pedoOn");
}

bool testStationaryToLightActive_steps()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);

    f.touchRaw_val            = true;
    f.stepsLastMinute_val     = 5;        // steps > 0 triggers exit
    f.isHrValidLastMinute_val = true;     // keep fake-touch guard inactive
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::LIGHT_ACTIVE,
                       "s->la.steps");
}

bool testStationaryToLightActive_motion()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);

    f.touchRaw_val            = true;
    f.motionContinuousSec     = 30;       // isMotion(30) true
    f.isHrValidLastMinute_val = true;
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::LIGHT_ACTIVE,
                       "s->la.motion");
}

bool testLightActiveToActive()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::LIGHT_ACTIVE);

    f.touchRaw_val        = true;
    f.stepsLastMinute_val = 25;           // >= 20 triggers ACTIVE
    fsm.tick();

    if (!expectState(fsm.current(), Fsm::State::ACTIVE, "la->a.state")) {
        return false;
    }
    if (!expectSensor(s, FsmSensor::FLOOR, true, "la->a.floorOn")) {
        return false;
    }
    return expectSensor(s, FsmSensor::HR, true, "la->a.hrOn");
}

bool testActiveToLightActive_after120s()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::ACTIVE);

    // Sit in ACTIVE for >= 120 s with low steps; should drop down.
    // isActuallyTouched must stay true so the no-touch exit doesn't
    // fire first and beat us to UNWORN via a different path.
    f.nowMs_val            += 121u * 1000u;
    f.isActuallyTouched_val = true;
    f.stepsLastMinute_val   = 10;
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::LIGHT_ACTIVE,
                       "a->la");
}

bool testStationaryToUnworn_noTouch60s()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);

    f.nowMs_val           += 61u * 1000u;
    f.isActuallyTouched_val = false;
    f.touchRaw_val          = false;
    f.motionRaw_val         = false;
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::UNWORN, "s->u.noTouch");
}

bool testStationaryToUnworn_fakeTouch()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);

    // touchRaw still true (something on the wrist registers), but no
    // real motion - fake-touch path to UNWORN. motionRaw must be
    // false too, otherwise UNWORN's `touch && motion` re-entry
    // bounces us straight back into STATIONARY.
    f.touchRaw_val            = true;
    f.motionRaw_val           = false;
    f.noMotionContinuousSec   = 15u * 60u;
    f.stepsLastMinute_val     = 0;
    f.isHrValidLastMinute_val = false;
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::UNWORN, "s->u.fake");
}

bool testLightActiveToStationary_settled()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::LIGHT_ACTIVE);

    // touch && noMotion(180) && steps==0 && hrValid -> drop back to
    // STATIONARY (HR-confirmed settle).
    f.touchRaw_val            = true;
    f.noMotionContinuousSec   = 180;
    f.stepsLastMinute_val     = 0;
    f.isHrValidLastMinute_val = true;
    fsm.tick();

    return expectState(fsm.current(), Fsm::State::STATIONARY,
                       "la->s.settled");
}

bool testActiveSensorMap()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::ACTIVE);

    if (!expectSensor(s, FsmSensor::TOUCH,  true, "active.touch"))  return false;
    if (!expectSensor(s, FsmSensor::MOTION, true, "active.motion")) return false;
    if (!expectSensor(s, FsmSensor::PEDO,   true, "active.pedo"))   return false;
    if (!expectSensor(s, FsmSensor::HR,     true, "active.hr"))     return false;
    return expectSensor(s, FsmSensor::FLOOR, true, "active.floor");
}

bool testUnwornSensorMap()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::UNWORN);

    if (!expectSensor(s, FsmSensor::TOUCH,  true,  "unworn.touch"))  return false;
    if (!expectSensor(s, FsmSensor::MOTION, true,  "unworn.motion")) return false;
    if (!expectSensor(s, FsmSensor::PEDO,   false, "unworn.pedo"))   return false;
    if (!expectSensor(s, FsmSensor::HR,     false, "unworn.hr"))     return false;
    return expectSensor(s, FsmSensor::FLOOR, false, "unworn.floor");
}

bool testStationarySensorMap()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::STATIONARY);

    if (!expectSensor(s, FsmSensor::TOUCH,  true,  "stat.touch"))  return false;
    if (!expectSensor(s, FsmSensor::MOTION, true,  "stat.motion")) return false;
    if (!expectSensor(s, FsmSensor::PEDO,   true,  "stat.pedo"))   return false;
    // HR may be off (no burst yet) or on (probe in flight). Don't
    // pin the exact value - just assert FLOOR is off (key invariant).
    return expectSensor(s, FsmSensor::FLOOR, false, "stat.floor");
}

bool testLightActiveSensorMap()
{
    MockFeatures f;
    MockSensors  s;
    Fsm fsm(f, s);
    driveTo(fsm, f, s, Fsm::State::LIGHT_ACTIVE);

    if (!expectSensor(s, FsmSensor::TOUCH,  true,  "la.touch"))  return false;
    if (!expectSensor(s, FsmSensor::MOTION, true,  "la.motion")) return false;
    if (!expectSensor(s, FsmSensor::PEDO,   true,  "la.pedo"))   return false;
    return expectSensor(s, FsmSensor::FLOOR, false, "la.floor");
}

} // namespace

void TestActivity_Fsm_execute()
{
    LOG_INFO("Fsm tests start\n");

    if (!testInitialStateIsIdle())              { LOG_ERROR("FAIL\n"); return; }
    if (!testResetReturnsToIdle())              { LOG_ERROR("FAIL\n"); return; }
    if (!testIdleEntryDisconnectsAll())         { LOG_ERROR("FAIL\n"); return; }
    if (!testIdleToUnwornOnFirstTick())         { LOG_ERROR("FAIL\n"); return; }
    if (!testUnwornStaysIdleWhenNoTouchNoMotion()) { LOG_ERROR("FAIL\n"); return; }
    if (!testUnwornToStationary())              { LOG_ERROR("FAIL\n"); return; }
    if (!testStationaryToLightActive_steps())   { LOG_ERROR("FAIL\n"); return; }
    if (!testStationaryToLightActive_motion())  { LOG_ERROR("FAIL\n"); return; }
    if (!testLightActiveToActive())             { LOG_ERROR("FAIL\n"); return; }
    if (!testActiveToLightActive_after120s())   { LOG_ERROR("FAIL\n"); return; }
    if (!testStationaryToUnworn_noTouch60s())   { LOG_ERROR("FAIL\n"); return; }
    if (!testStationaryToUnworn_fakeTouch())    { LOG_ERROR("FAIL\n"); return; }
    if (!testLightActiveToStationary_settled()) { LOG_ERROR("FAIL\n"); return; }
    if (!testUnwornSensorMap())                 { LOG_ERROR("FAIL\n"); return; }
    if (!testStationarySensorMap())             { LOG_ERROR("FAIL\n"); return; }
    if (!testLightActiveSensorMap())            { LOG_ERROR("FAIL\n"); return; }
    if (!testActiveSensorMap())                 { LOG_ERROR("FAIL\n"); return; }

    LOG_INFO("Fsm tests PASS\n");
}
