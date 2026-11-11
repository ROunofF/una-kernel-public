#define LOG_MODULE_PRX      "TestSensorXL"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestSensorXL.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"
#include "System.hpp"
#include "OS.hpp"
#include "Sleep.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "Test/SensorProbe.hpp"

#include <cstdint>

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorXL : public SDK::Interface::ISensorDataListener,
                     public SensorProbeTest
{
public:
    TestSensorXL()
        : mSensorAccelRaw(SDK::Sensor::Type::ACCELEROMETER_RAW, this, 1000 / 50, 1000)
        , mSlow("XL 40/100", SDK::Sensor::Type::ACCELEROMETER)
        , mSlowToo("XL 40/1000", SDK::Sensor::Type::ACCELEROMETER)
        , mRaw("XLraw 20/1000", SDK::Sensor::Type::ACCELEROMETER_RAW)
        , mRawToo("XLraw 40/100", SDK::Sensor::Type::ACCELEROMETER_RAW)
        , mFusionRaw("FusionRaw 10/100", SDK::Sensor::Type::FUSION_RAW)
        , mGyro("Gyro 20/200", SDK::Sensor::Type::GYROSCOPE)
    {
    }

    /// Print the raw accelerometer for two minutes.
    void run()
    {
        mSensorAccelRaw.connect();

        ::Driver::SwTimer t(120000);
        for (;;) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        mSensorAccelRaw.disconnect();
    }

    /**
     * @brief What a listener's period and latency come to, end to end.
     *
     * The whole sensor layer with nothing else of the system running. Every
     * scenario measures how often a listener is given data, how many samples
     * a delivery carries, how old the oldest of them is by then and whether
     * the timestamps run on, prints two lines and says PASS or FAIL. Keep the
     * watch still.
     *
     * A listener's latency is how late its oldest sample may be. The IMU
     * gathers readings in its FIFO and is read when the FIFO has that much,
     * so the latency asked for has to reach the FIFO; a FIFO read once a
     * second makes every listener a second late whatever it asked for. And
     * the listener is handed what it holds when that cannot wait for the next
     * batch, not only when its queue is full, so a delivery carries up to
     * the samples that lie within the latency and not always that many.
     */
    void run_latency()
    {
        LOG_INFO("=== Sensor latency test ===\n");

        mFailures = 0;

        // 40 ms and 100 ms: two frames of 25 Hz in a read, a delivery every
        // 80 ms, the oldest sample 80 ms and what the path adds.
        const Expect kTwoFrames  { 70, 95, 15, 30, 140 };
        // The same listener beside a 50 Hz one: five frames in 100 ms, every
        // second one let through to a 40 ms listener, two or three of them
        // in one delivery a read.
        const Expect kBeside50Hz { 88, 112, 20, 30, 140 };
        // A second asked for. Alone the source gives a batch a second and the
        // listener a delivery a batch. Beside a prompt listener the batches
        // come every 80 or 100 ms, and the delivery goes a batch before its
        // oldest sample would be a second old.
        const Expect kSecond50Hz { 850, 1100, 250, 600, 1100 };
        const Expect kSecond25Hz { 850, 1100, 120, 300, 1100 };
        const Expect kHalfSecond { 420, 560, 90, 150, 640 };

        // L1. One listener, 40 ms and 100 ms.
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 5000, true);
        check("L1 40/100 alone", mSlow, kTwoFrames);

        // L1b. The same again with a listener of the raw stream beside it, to
        //      tell where a late delivery was held: both see it, or only the
        //      one the data is worked over for.
        mRawToo.connect(40, 100);
        OS::Delay(1500);
        mSlow.restart(kTwoFrames.gapMaxMs, kTwoFrames.oldestAvgMaxMs);
        mRawToo.restartAll(kTwoFrames.gapMaxMs, kTwoFrames.oldestAvgMaxMs);
        OS::Delay(10000);
        check("L1b 40/100", mSlow, kTwoFrames);
        check("L1b raw 40/100", mRawToo, kTwoFrames);
        mRawToo.disconnect();
        OS::Delay(500);

        // L2. A 20 ms listener with a latency of a second joins. The first
        //     one must stay as prompt as it was; the second is given its
        //     data once a second.
        mRaw.traceShortGaps(true);
        mSlowToo.traceShortGaps(true);
        mRaw.connect(20, 1000);
        OS::Delay(1500);
        mSlow.restart(kBeside50Hz.gapMaxMs, kBeside50Hz.oldestAvgMaxMs);
        mRaw.restartAll(kSecond50Hz.gapMaxMs, kSecond50Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("L2 40/100 beside 20/1000", mSlow, kBeside50Hz);
        check("L2 20/1000", mRaw, kSecond50Hz);

        // L10. How many samples a second a 40 ms listener is given from a
        //      50 Hz stream. No verdict: the rate adapter lets through every
        //      second sample or all of them by which side of half its period
        //      the step of the stream falls, and that is this part's own.
        {
            const Probe::Snapshot s = mSlow.get();
            const uint32_t elapsed  = s.lastMs - s.firstMs;
            const uint32_t perSec10 = (elapsed != 0) ? (s.laterSamples * 10000u) / elapsed : 0;

            LOG_INFO("[L10 40 ms from 50 Hz] %lu.%lu samples a second, step %lu..%lu us\n",
                     static_cast<unsigned long>(perSec10 / 10),
                     static_cast<unsigned long>(perSec10 % 10),
                     static_cast<unsigned long>(s.minStepUs),
                     static_cast<unsigned long>(s.maxStepUs));
        }

        // L3. The prompt listener leaves: the other goes on once a second.
        mSlow.disconnect();
        OS::Delay(1500);
        mRaw.restart(kSecond50Hz.gapMaxMs, kSecond50Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("L3 20/1000 left alone", mRaw, kSecond50Hz);
        mRaw.disconnect();
        OS::Delay(500);

        // L4. One listener asks again with another latency, and again. It
        //     stays connected throughout, and its data must run on: no sample
        //     twice, none stamped zero, none out of order.
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 4000, true);
        check("L4 40/100", mSlow, kTwoFrames);
        mSlow.connect(40, 500);
        measure(mSlow, kHalfSecond, 5000, false);
        check("L4 -> 40/500", mSlow, kHalfSecond);
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 4000, false);
        check("L4 -> 40/100", mSlow, kTwoFrames);
        mSlow.disconnect();
        OS::Delay(500);

        // L5. Two listeners of one sensor, a second and 100 ms, coming and
        //     going in both orders: the tighter of those present sets the pace
        //     for the source, and each is given what it asked for.
        mSlowToo.connect(40, 1000);
        mSlow.connect(40, 100);
        OS::Delay(1500);
        mSlow.restartAll(kTwoFrames.gapMaxMs, kTwoFrames.oldestAvgMaxMs);
        mSlowToo.restartAll(kSecond25Hz.gapMaxMs, kSecond25Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("L5 40/100 second in", mSlow, kTwoFrames);
        check("L5 40/1000 first in", mSlowToo, kSecond25Hz);
        mSlow.disconnect();
        OS::Delay(1500);
        mSlowToo.restart(kSecond25Hz.gapMaxMs, kSecond25Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("L5 40/1000 left alone", mSlowToo, kSecond25Hz);
        mSlowToo.disconnect();
        OS::Delay(500);

        mSlow.connect(40, 100);
        mSlowToo.connect(40, 1000);
        OS::Delay(1500);
        mSlow.restartAll(kTwoFrames.gapMaxMs, kTwoFrames.oldestAvgMaxMs);
        mSlowToo.restartAll(kSecond25Hz.gapMaxMs, kSecond25Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("L5 40/100 first in", mSlow, kTwoFrames);
        check("L5 40/1000 second in", mSlowToo, kSecond25Hz);
        mSlowToo.disconnect();
        OS::Delay(1500);
        mSlow.restart(kTwoFrames.gapMaxMs, kTwoFrames.oldestAvgMaxMs);
        OS::Delay(5000);
        check("L5 40/100 left alone", mSlow, kTwoFrames);
        mSlow.disconnect();
        OS::Delay(500);

        // L6. Both streams at 100 Hz through the fusion inputs.
        const Expect kTenFrames { 88, 112, 80, 120, 140 };
        mFusionRaw.connect(10, 100);
        measure(mFusionRaw, kTenFrames, 5000, true);
        check("L6 fusion raw 10/100", mFusionRaw, kTenFrames);
        mFusionRaw.disconnect();
        OS::Delay(500);

        // L7. The processed gyroscope: the latency has to get through the
        //     sub-sensor that works the data over on its way.
        const Expect kGyro { 176, 224, 80, 120, 260 };
        mGyro.connect(20, 200);
        measure(mGyro, kGyro, 5000, true);
        check("L7 gyro 20/200", mGyro, kGyro);
        mGyro.disconnect();
        OS::Delay(500);

        // L8. Twenty quick rounds of connecting and disconnecting, then one
        //     that stays: it must work as the first did, and when it goes
        //     the data must stop.
        for (int i = 0; i < 20; ++i) {
            mSlow.connect(40, 100);
            OS::Delay(120);
            mSlow.disconnect();
            OS::Delay(30);
        }
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 4000, true);
        check("L8 after 20 rounds", mSlow, kTwoFrames);
        mSlow.disconnect();
        OS::Delay(500);
        mSlow.restartAll(0, 0);
        OS::Delay(2000);
        {
            const Probe::Snapshot s = mSlow.get();
            const bool ok = (s.deliveries == 0);

            LOG_INFO("[L8 disconnected] %lu deliveries in 2 s -> %s\n",
                     static_cast<unsigned long>(s.deliveries), ok ? "PASS" : "FAIL");
            if (!ok) {
                ++mFailures;
            }
        }

        // L9. A minute of L1: how late deliveries come with nothing else of
        //     the system running.
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 60000, true);
        check("L9 one minute", mSlow, kTwoFrames);
        {
            const Probe::Snapshot s = mSlow.get();

            LOG_INFO("[L9 one minute] late deliveries %lu of %lu, worst gap %lu ms, oldest sample %ld ms at the most, "
                     "over %lu ms in %lu deliveries\n",
                     static_cast<unsigned long>(s.lateGaps),
                     static_cast<unsigned long>(s.gaps),
                     static_cast<unsigned long>(s.maxGapMs),
                     static_cast<long>(s.maxOldestMs),
                     static_cast<unsigned long>(kTwoFrames.oldestAvgMaxMs),
                     static_cast<unsigned long>(s.stale));
        }
        mSlow.disconnect();
        OS::Delay(500);

        // L11. L9 again with the deep sleep held off. Its cadence is judged like
        //      any other line's, its count of late deliveries is not: that
        //      tells whether the deliveries that come a read late do so
        //      because the interrupt that announces the read is lost on the
        //      way into the sleep.
        Driver::Sleep::Pause();
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 60000, true);
        check("L11 one minute, no deep sleep", mSlow, kTwoFrames);
        {
            const Probe::Snapshot s = mSlow.get();

            LOG_INFO("[L11 one minute, no deep sleep] late deliveries %lu of %lu, worst gap %lu ms\n",
                     static_cast<unsigned long>(s.lateGaps),
                     static_cast<unsigned long>(s.gaps),
                     static_cast<unsigned long>(s.maxGapMs));
        }
        mSlow.disconnect();
        Driver::Sleep::Resume();

        LOG_INFO("Sensor latency test completed: %lu failure(s)\n", static_cast<unsigned long>(mFailures));
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        if (count == 0) {
            return;
        }

        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorAccelRaw.matchesDriver(handle)) {
            SDK::SensorDataParser::AccelerometerRaw p(batch[0]);
            LOG_INFO("%6d %6d %6d\r\n", (int)p.getX(), (int)p.getY(), (int)p.getZ());
        }
    }

private:
    Sensor::Connection mSensorAccelRaw;

    Probe mSlow;
    Probe mSlowToo;
    Probe mRaw;
    Probe mRawToo;
    Probe mFusionRaw;
    Probe mGyro;
};

////////////////////////////////////////////////////////////////////////////////
//// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestSensorXL_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorXL test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        //test.run();
        test.run_latency();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
