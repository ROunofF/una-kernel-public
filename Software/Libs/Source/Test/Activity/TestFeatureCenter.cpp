/**
 ******************************************************************************
 * @file    TestFeatureCenter.cpp
 * @brief   Unit tests for Activity::FeatureCenter.
 *
 *          Focuses on FeatureCenter's unique behaviour (the trackers
 *          themselves have their own tests):
 *           - IFsmTrigger::requestFsmTick() fires on rising motion /
 *             touch edges only, never on falling edges or no-change
 *             updates.
 *           - Trigger is optional (no trigger attached must not
 *             crash).
 *           - raw() accessors reflect the latest onUpdate state.
 *
 *          Time-window queries (stepsLastMinute, isMotion, etc.)
 *          and HR/Activity helpers use OS::GetTimeMs() inside, which
 *          makes deterministic testing brittle on real wall-clock;
 *          those are exercised by the per-tracker unit tests and by
 *          the runtime sensor test.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestFC"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/Activity/TestFeatureCenter.hpp"
#include "Activity/FeatureCenter.hpp"
#include "Activity/IFsmTrigger.hpp"

namespace {

class CountingTrigger : public Activity::IFsmTrigger
{
public:
    void     requestFsmTick() override { ++mCount; }
    uint32_t count() const             { return mCount; }
    void     reset()                   { mCount = 0; }

private:
    uint32_t mCount = 0;
};

bool expectEq(uint32_t got, uint32_t want, const char* scenario)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got %u, want %u\n",
              scenario, (unsigned)got, (unsigned)want);
    return false;
}

bool expectBool(bool got, bool want, const char* scenario)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got %d, want %d\n",
              scenario, (int)got, (int)want);
    return false;
}

// Without an attached trigger, sensor edges must not crash. This is
// the boot-time state (FeatureCenter constructed before attachTrigger
// has been called).
bool testNoTriggerAttachedIsSafe()
{
    Activity::FeatureCenter fc;

    fc.onMotionUpdate(true,  0);
    fc.onMotionUpdate(false, 100);
    fc.onTouchUpdate(true,   200);
    fc.onTouchUpdate(false,  300);

    // Reaching here without faulting is the pass condition.
    return true;
}

// Motion: rising edge fires the trigger exactly once; subsequent
// "still detected" updates produce no extra triggers.
bool testMotionRisingEdgeOnce()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onMotionUpdate(false, 0);    // initial off, no edge
    if (!expectEq(trig.count(), 0, "motion@idle->idle")) return false;

    fc.onMotionUpdate(true, 100);   // rising edge
    if (!expectEq(trig.count(), 1, "motion@rising")) return false;

    fc.onMotionUpdate(true, 200);   // still motion, no new edge
    if (!expectEq(trig.count(), 1, "motion@steady-on")) return false;

    fc.onMotionUpdate(true, 300);   // still motion, no new edge
    return expectEq(trig.count(), 1, "motion@steady-on-2");
}

// Falling motion edge must NOT trigger. Only rising edges drive the
// out-of-cadence FSM wake-up; falling edges ride the next periodic
// tick.
bool testMotionFallingEdgeIgnored()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onMotionUpdate(true,  0);    // rising, trigger #1
    fc.onMotionUpdate(false, 100);  // falling, no trigger
    return expectEq(trig.count(), 1, "motion@falling");
}

// off -> on -> off -> on triggers twice (once for each rising edge).
bool testMotionMultipleRisingEdges()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onMotionUpdate(true,  0);    // edge #1
    fc.onMotionUpdate(false, 100);
    fc.onMotionUpdate(true,  200);  // edge #2
    fc.onMotionUpdate(false, 300);
    fc.onMotionUpdate(true,  400);  // edge #3
    return expectEq(trig.count(), 3, "motion@multipleEdges");
}

// Same edge rules for touch.
bool testTouchRisingEdgeOnce()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onTouchUpdate(false, 0);
    if (!expectEq(trig.count(), 0, "touch@idle->idle")) return false;

    fc.onTouchUpdate(true, 100);
    if (!expectEq(trig.count(), 1, "touch@rising")) return false;

    fc.onTouchUpdate(true, 200);
    return expectEq(trig.count(), 1, "touch@steady-on");
}

bool testTouchFallingEdgeIgnored()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onTouchUpdate(true,  0);
    fc.onTouchUpdate(false, 100);
    return expectEq(trig.count(), 1, "touch@falling");
}

// Step events are not edge-triggered (steps ride the cadence-only
// path). onStepCounter / resetStepCounter must NOT poke the FSM
// trigger - that channel is reserved for touch / motion edges.
bool testStepEventsDoNotTrigger()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onStepCounter(10, 0);
    fc.onStepCounter(20, 100);
    fc.resetStepCounter(0, 200);
    return expectEq(trig.count(), 0, "stepEventsDoNotTrigger");
}

// HR samples must not trigger the FSM either.
bool testHrEventsDoNotTrigger()
{
    Activity::FeatureCenter fc;
    CountingTrigger         trig;
    fc.attachTrigger(trig);

    fc.onHrSample(75, 2, 0);
    fc.onHrSample(80, 2, 100);
    return expectEq(trig.count(), 0, "hrEventsDoNotTrigger");
}

// raw() accessors reflect the latest state pushed in. Used by the
// FSM to read motion / touch without time-window logic.
bool testMotionRawReflectsLastUpdate()
{
    Activity::FeatureCenter fc;

    if (!expectBool(fc.motionRaw(), false, "motionRaw@initial")) return false;

    fc.onMotionUpdate(true, 0);
    if (!expectBool(fc.motionRaw(), true, "motionRaw@on")) return false;

    fc.onMotionUpdate(false, 100);
    return expectBool(fc.motionRaw(), false, "motionRaw@off");
}

bool testTouchRawReflectsLastUpdate()
{
    Activity::FeatureCenter fc;

    if (!expectBool(fc.touchRaw(), false, "touchRaw@initial")) return false;

    fc.onTouchUpdate(true, 0);
    if (!expectBool(fc.touchRaw(), true, "touchRaw@on")) return false;

    fc.onTouchUpdate(false, 100);
    return expectBool(fc.touchRaw(), false, "touchRaw@off");
}

// IFeatureSource is the same object as FeatureCenter, but going
// through the interface pointer must yield identical results - the
// FSM only ever sees the interface.
bool testIFeatureSourceInterfaceMatches()
{
    Activity::FeatureCenter   fc;
    Activity::IFeatureSource& iface = fc;

    fc.onMotionUpdate(true,  0);
    fc.onTouchUpdate(true,   0);

    if (!expectBool(iface.motionRaw(), true, "iface.motionRaw")) return false;
    return expectBool(iface.touchRaw(), true, "iface.touchRaw");
}

} // namespace

void TestActivity_FeatureCenter_execute()
{
    LOG_INFO("FeatureCenter tests start\n");

    if (!testNoTriggerAttachedIsSafe())     { LOG_ERROR("FAIL\n"); return; }
    if (!testMotionRisingEdgeOnce())        { LOG_ERROR("FAIL\n"); return; }
    if (!testMotionFallingEdgeIgnored())    { LOG_ERROR("FAIL\n"); return; }
    if (!testMotionMultipleRisingEdges())   { LOG_ERROR("FAIL\n"); return; }
    if (!testTouchRisingEdgeOnce())         { LOG_ERROR("FAIL\n"); return; }
    if (!testTouchFallingEdgeIgnored())     { LOG_ERROR("FAIL\n"); return; }
    if (!testStepEventsDoNotTrigger())      { LOG_ERROR("FAIL\n"); return; }
    if (!testHrEventsDoNotTrigger())        { LOG_ERROR("FAIL\n"); return; }
    if (!testMotionRawReflectsLastUpdate()) { LOG_ERROR("FAIL\n"); return; }
    if (!testTouchRawReflectsLastUpdate())  { LOG_ERROR("FAIL\n"); return; }
    if (!testIFeatureSourceInterfaceMatches()) { LOG_ERROR("FAIL\n"); return; }

    LOG_INFO("FeatureCenter tests PASS\n");
}
