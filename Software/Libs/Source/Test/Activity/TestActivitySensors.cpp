/**
 ******************************************************************************
 * @file    TestActivitySensors.cpp
 * @brief   Hardware test for the five sensors Activity::Fsm controls.
 *
 *          Walks through the on/off combinations the FSM uses across
 *          its states (UNWORN, STATIONARY, LIGHT_ACTIVE, ACTIVE) and
 *          checks one key invariant: PEDO and FLOOR counters must NOT
 *          decrease across a disconnect/reconnect cycle.
 *
 *          Interactive: log messages prompt actions in each phase.
 *          Read the log alongside running.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestActSens"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/Activity/TestActivitySensors.hpp"
#include "SwTimer.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp"

namespace {

void waitMs(uint32_t ms)
{
    ::Driver::SwTimer t(ms);
    while (!t.expired()) {
        OS::Delay(100);
    }
}

class TestActivitySensors : public SDK::Interface::ISensorDataListener
{
public:
    TestActivitySensors()
        : mTouch(SDK::Sensor::Type::TOUCH_DETECT, this)
        , mMotion(SDK::Sensor::Type::MOTION_DETECT, this)
        , mPedo(SDK::Sensor::Type::STEP_COUNTER, this)
        , mHr(SDK::Sensor::Type::HEART_RATE, this)
        , mFloor(SDK::Sensor::Type::FLOOR_COUNTER, this)
    {
    }

    void run()
    {
        mLatestSteps          = 0;
        mLatestFloorsUp       = 0;
        mLatestFloorsDown     = 0;
        mInlineRegressions    = 0;

        phaseUnworn();
        phaseStationaryHrBurst();
        phaseActive();
        phasePedoMonotonicity();
        phaseFloorMonotonicity();

        summary();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mTouch.matchesDriver(handle)) {
            SDK::SensorDataParser::Touch p(batch[0]);
            LOG_INFO("touch  : %d\n", (int)p.isTouched());
        } else if (mMotion.matchesDriver(handle)) {
            SDK::SensorDataParser::MotionDetect p(batch[0]);
            LOG_INFO("motion : id=%d\n", (int)p.getID());
        } else if (mPedo.matchesDriver(handle)) {
            SDK::SensorDataParser::StepCounter p(batch[0]);
            const uint32_t s = p.getStepCount();
            if (s < mLatestSteps) {
                LOG_ERROR("INLINE FAIL: steps regressed %u -> %u\n",
                          (unsigned)mLatestSteps, (unsigned)s);
                ++mInlineRegressions;
            }
            mLatestSteps = s;
            LOG_INFO("steps  : %u\n", (unsigned)s);
        } else if (mHr.matchesDriver(handle)) {
            SDK::SensorDataParser::HeartRate p(batch[0]);
            if (p.isDataValid()) {
                LOG_INFO("hr     : %.1f tl=%d\n",
                         p.getBpm(), (int)p.getTrustLevel());
            } else {
                LOG_INFO("hr     : invalid\n");
            }
        } else if (mFloor.matchesDriver(handle)) {
            SDK::SensorDataParser::FloorCounter p(batch[0]);
            const int32_t up = p.getFloorsUp();
            const int32_t dn = p.getFloorsDown();
            if (up < (int32_t)mLatestFloorsUp || dn < (int32_t)mLatestFloorsDown) {
                LOG_ERROR("INLINE FAIL: floors regressed up %u->%d dn %u->%d\n",
                          (unsigned)mLatestFloorsUp, (int)up,
                          (unsigned)mLatestFloorsDown, (int)dn);
                ++mInlineRegressions;
            }
            mLatestFloorsUp   = (uint32_t)up;
            mLatestFloorsDown = (uint32_t)dn;
            LOG_INFO("floors : up=%d dn=%d\n", (int)up, (int)dn);
        }
    }

private:
    void banner(const char* title)
    {
        LOG_INFO_WP("\n========== %s ==========\n", title);
    }

    // --- Phase 1 ---------------------------------------------------------
    // FSM in UNWORN runs only TOUCH + MOTION. Confirms these two coexist
    // and publish independently.
    void phaseUnworn()
    {
        banner("Phase 1: UNWORN (TOUCH + MOTION)");
        LOG_INFO("Touch the screen / wave the device around for ~8 s\n");

        mTouch.connect();
        mMotion.connect();
        waitMs(60000);
        mTouch.disconnect();
        mMotion.disconnect();
    }

    // --- Phase 2 ---------------------------------------------------------
    // Verifies HR bursts on/off while PEDO + MOTION stay up.
    // After each "HR burst END" line you should see NO further HR logs
    // until the next "HR burst START". If you see HR logs while it is
    // supposedly off, something keeps PPG warm.
    void phaseStationaryHrBurst()
    {
        banner("Phase 2: STATIONARY + HR burst (PPG-drop check)");
        LOG_INFO("Wear the watch, stay still. Two HR bursts will fire;\n");
        LOG_INFO("no HR samples must arrive between END and next START.\n");

        mTouch.connect();
        mMotion.connect();
        mPedo.connect();
        waitMs(30000);

        LOG_INFO_WP("\n>>> HR burst #1 START\n");
        mHr.connect();
        waitMs(8000);
        LOG_INFO_WP(">>> HR burst #1 END (expect no HR samples for ~30 s)\n");
        mHr.disconnect();
        waitMs(30000);

        LOG_INFO_WP("\n>>> HR burst #2 START\n");
        mHr.connect();
        waitMs(8000);
        LOG_INFO_WP(">>> HR burst #2 END (expect no HR samples for ~30 s)\n");
        mHr.disconnect();
        waitMs(30000);

        mPedo.disconnect();
        mMotion.disconnect();
        mTouch.disconnect();
    }

    // --- Phase 3 ---------------------------------------------------------
    // Worst-case: every Activity-controlled sensor on simultaneously.
    // Verifies they coexist without queue overflows / starvation.
    void phaseActive()
    {
        banner("Phase 3: ACTIVE (all five sensors, HR continuous)");
        LOG_INFO("Take a brisk walk for ~30 s\n");

        mTouch.connect();
        mMotion.connect();
        mPedo.connect();
        mFloor.connect();
        mHr.connect();
        waitMs(30000);
        mHr.disconnect();
        mFloor.disconnect();
        mPedo.disconnect();
        mMotion.disconnect();
        mTouch.disconnect();

        waitMs(1000);
    }

    // --- Phase 4 ---------------------------------------------------------
    // ImuSubSensorStepCounter::stop() folds the session counter into
    // the offset before disabling BMI270, so across a disconnect /
    // reconnect cycle the published cumulative value must not regress.
    void phasePedoMonotonicity()
    {
        banner("Phase 4: PEDO monotonicity across reconnect");
        LOG_INFO("Walk a few steps in each window. We compare before\n");
        LOG_INFO("disconnect vs after reconnect; the count must NOT drop.\n");

        mPedo.connect();
        waitMs(20000);
        const uint32_t before = mLatestSteps;
        LOG_INFO_WP("\nSteps observed before disconnect: %u\n", (unsigned)before);

        mPedo.disconnect();
        LOG_INFO("PEDO disconnected. Walking 10 s with sensor OFF.\n");
        LOG_INFO("(Steps in this window will be visible only on reconnect.)\n");
        waitMs(10000);

        mPedo.connect();
        waitMs(20000);
        const uint32_t after = mLatestSteps;
        LOG_INFO_WP("Steps observed after reconnect:  %u\n", (unsigned)after);

        if (after >= before) {
            LOG_INFO("PEDO MONOTONIC: OK (%u -> %u, delta %u)\n",
                     (unsigned)before, (unsigned)after,
                     (unsigned)(after - before));
        } else {
            LOG_ERROR("PEDO MONOTONIC: FAIL (%u -> %u, regressed by %u)\n",
                      (unsigned)before, (unsigned)after,
                      (unsigned)(before - after));
        }

        mPedo.disconnect();
        waitMs(2000);
    }

    // --- Phase 5 ---------------------------------------------------------
    // Floor counter lives in RAM (FloorCounter accumulator), so this is
    // expected to be trivially monotonic. Still useful as a regression
    // guard for the uint16 -> uint32 widening of the floor counters.
    void phaseFloorMonotonicity()
    {
        banner("Phase 5: FLOOR monotonicity across reconnect");
        LOG_INFO("(Floor counts rarely change inside a single session;\n");
        LOG_INFO(" the check verifies the published values do not regress.)\n");

        mFloor.connect();
        waitMs(5000);
        const uint32_t upBefore = mLatestFloorsUp;
        const uint32_t dnBefore = mLatestFloorsDown;

        mFloor.disconnect();
        waitMs(5000);

        mFloor.connect();
        waitMs(3000);
        const uint32_t upAfter = mLatestFloorsUp;
        const uint32_t dnAfter = mLatestFloorsDown;

        if (upAfter >= upBefore && dnAfter >= dnBefore) {
            LOG_INFO("FLOOR MONOTONIC: OK (up %u->%u, dn %u->%u)\n",
                     (unsigned)upBefore, (unsigned)upAfter,
                     (unsigned)dnBefore, (unsigned)dnAfter);
        } else {
            LOG_ERROR("FLOOR MONOTONIC: FAIL (up %u->%u, dn %u->%u)\n",
                      (unsigned)upBefore, (unsigned)upAfter,
                      (unsigned)dnBefore, (unsigned)dnAfter);
        }

        mFloor.disconnect();
        waitMs(2000);
    }

    void summary()
    {
        banner("Test summary");
        if (mInlineRegressions == 0) {
            LOG_INFO("No inline regressions observed\n");
        } else {
            LOG_ERROR("INLINE regressions seen: %u\n",
                      (unsigned)mInlineRegressions);
        }
    }

    Sensor::Connection mTouch;
    Sensor::Connection mMotion;
    Sensor::Connection mPedo;
    Sensor::Connection mHr;
    Sensor::Connection mFloor;

    uint32_t mLatestSteps       = 0;
    uint32_t mLatestFloorsUp    = 0;
    uint32_t mLatestFloorsDown  = 0;
    uint32_t mInlineRegressions = 0;
};

} // namespace

void TestActivitySensors_execute()
{
    LOG_INFO("Activity-sensors hardware test starting\n");
    Instance::SensorLayer::getInstance().init();

    TestActivitySensors test;

    OS::Delay(3000);

    while (1) {
        LOG_INFO_WP("\n////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// Activity-sensors test cycle\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();

        LOG_INFO_WP("\nIdle 10 s before next cycle\n");
        OS::Delay(10000);
    }
}
