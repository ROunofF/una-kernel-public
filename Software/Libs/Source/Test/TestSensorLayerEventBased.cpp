#define LOG_MODULE_PRX      "TestEvtSensor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorLayerEventBased.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"

#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorData.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

// Synthetic event-based sensor. Mirrors the production pattern: the producer
// path only marks data dirty and wakes the manager; sensorRefresh() publishes.
// sdcGetMinPeriod() reports a deliberately tiny period - a periodic sensor with
// it would force a fast manager tick; an event-based one must be ignored.
class EventBasedProbe : public Interface::ISensor,
                        public Sensor::ISensorDriverCtrl
{
public:
    EventBasedProbe()
        : mDriver(*this,
                  SDK::Sensor::Type::UNKNOWN,
                  1,
                  *this,
                  Sensor::Driver::Mode::EVENT_BASED)
    {
        mDataMutex.init(LOG_MODULE_PRX);
    }

    Sensor::Driver& getDriver()
    {
        return mDriver;
    }

    //// ISensorDriverCtrl
    float sdcStart(Sensor::Driver* driver, float period) override
    {
        (void) driver;
        return period;
    }

    void sdcStop(Sensor::Driver* driver) override
    {
        (void) driver;
    }

    float sdcUpdatePeriod(Sensor::Driver* driver, float period) override
    {
        (void) driver;
        return period;
    }

    float sdcGetMinPeriod(Sensor::Driver* driver) override
    {
        (void) driver;
        return 2.0f;
    }

    const char* sdcGetDescription(Sensor::Driver* driver) override
    {
        (void) driver;
        return "Event-based probe";
    }

    //// ISensor
    void sensorRefresh() override
    {
        mRefreshCount++;

        OS::MutexCS cs(mDataMutex);
        if (mDirty) {
            mDirty = false;
            publishLocked();
        }
    }

    // Producer entry point: marks data and wakes the manager, the same way a
    // real event-based sensor callback does.
    void produce(uint32_t value)
    {
        {
            OS::MutexCS cs(mDataMutex);
            mValue     = value;
            mTimestamp = OS::GetTimeMs();
            mDirty     = true;
        }
        Sensor::Manager::getInstance().wake();
    }

    uint32_t getRefreshCount() const
    {
        return mRefreshCount;
    }

private:
    void publishLocked()
    {
        auto& sample = mDriver.getDataSample();
        sample.setTimestamp(mTimestamp);
        sample.u[0] = mValue;
        mDriver.pushDataSample();
    }

    Sensor::Driver    mDriver;
    volatile uint32_t mRefreshCount = 0;
    uint32_t          mValue        = 0;
    uint32_t          mTimestamp    = 0;
    bool              mDirty        = false;
    OS::Mutex         mDataMutex;
};

class ProbeListener : public SDK::Interface::ISensorDataListener
{
public:
    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        (void) handle;
        (void) count;
        (void) stride;

        mLastValue = base->mValue[0].u32;
        mCount++;
    }

    uint32_t getCount() const     { return mCount; }
    uint32_t getLastValue() const { return mLastValue; }

private:
    volatile uint32_t mCount     = 0;
    uint32_t          mLastValue = 0;
};

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestSensorLayerEventBased_execute()
{
    LOG_INFO("start\n");

    EventBasedProbe probe;
    ProbeListener   listener;

    if (!Sensor::Manager::getInstance().regDriver(&probe.getDriver())) {
        LOG_INFO("regDriver failed\n");
        return;
    }

    for (;;) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// EVENT-BASED SENSOR TEST\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        Sensor::Connection conn(&probe.getDriver(), &listener);
        if (!conn.connect()) {
            LOG_INFO("connect failed\n");
            OS::Delay(3000);
            continue;
        }

        ////////////////////////////////////////////////////////////
        //// Phase 1: with no events the manager thread must idle,
        //// not spin. Expect a ~1000 ms tick (a few refreshes / 5 s).
        ////////////////////////////////////////////////////////////

        OS::Delay(300); // let connect settle

        const uint32_t refreshBefore = probe.getRefreshCount();
        OS::Delay(5000);
        const uint32_t refreshDelta  = probe.getRefreshCount() - refreshBefore;

        const uint32_t impliedTickMs = (refreshDelta > 0) ? (5000 / refreshDelta) : 0;
        const bool     phase1Pass    = (refreshDelta >= 2) && (refreshDelta <= 8);

        LOG_INFO("phase1: refresh=%u / 5000ms, implied tick=%ums -> %s\n",
                 refreshDelta, impliedTickMs, phase1Pass ? "PASS" : "FAIL");

        ////////////////////////////////////////////////////////////
        //// Phase 2: produced data must reach the listener with the
        //// correct value and a short latency (proves wake()).
        ////////////////////////////////////////////////////////////

        bool phase2Pass = true;

        const uint32_t values[3] = { 111, 222, 333 };
        for (uint32_t idx = 0; idx < 3; ++idx) {
            const uint32_t before = listener.getCount();

            ::Driver::SwTimer t;
            t.start();
            probe.produce(values[idx]);

            uint32_t waited = 0;
            while (listener.getCount() == before && waited < 1500) {
                OS::Delay(5);
                waited += 5;
            }

            const uint32_t latency = t.passed();
            const bool     got     = (listener.getCount() != before);
            const bool     valOk   = got && (listener.getLastValue() == values[idx]);
            const bool     fast    = got && (latency < 500);
            const bool     ok      = valOk && fast;

            if (!ok) {
                phase2Pass = false;
            }

            LOG_INFO("phase2: value=%u recv=%u latency=%ums -> %s\n",
                     values[idx],
                     listener.getLastValue(),
                     latency,
                     ok ? "PASS" : "FAIL");

            OS::Delay(200);
        }

        conn.disconnect();

        LOG_INFO("RESULT: phase1=%s phase2=%s\n",
                 phase1Pass ? "PASS" : "FAIL",
                 phase2Pass ? "PASS" : "FAIL");

        LOG_INFO_WP("\n\n");
        OS::Delay(3000);
    }
}
