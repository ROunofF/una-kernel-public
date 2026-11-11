/**
 ******************************************************************************
 * @file    TestSensorSourceStep.cpp
 * @brief   On-device test of the rate a SensorLayer source is left running at
 *          as its listeners change
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSensorSourceStep"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestSensorSourceStep.hpp"
#include "Test/SensorProbe.hpp"
#include "OS.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

#include <cstdint>
#include <cstdio>

/**
 * @brief What rate a source is left running at, source by source.
 *
 * A source runs at the shortest period any of its listeners asks for. What
 * a listener is given does not show whether it does: each queue thins the
 * stream to its own period, so the samples are right with the source running
 * faster than anyone needs. Read here is the step the source reports
 * (Sensor::Driver::getSourceStepUs() of its channel) after every change of
 * the listeners, speeding up and slowing down.
 *
 * A part with a rate of its own reports the step with its samples, so each
 * reading waits for samples that came after the change.
 */
class TestSensorSourceStep
{
public:
    TestSensorSourceStep()
        : mAccel("accel", SDK::Sensor::Type::ACCELEROMETER_RAW, SDK::Sensor::Type::ACCELEROMETER, kPart)
        , mGyro("gyro", SDK::Sensor::Type::GYROSCOPE_RAW, SDK::Sensor::Type::GYROSCOPE, kPart)
        , mFusion("fusion", SDK::Sensor::Type::FUSION_RAW, SDK::Sensor::Type::FUSION, kPart)
        , mMag("mag", SDK::Sensor::Type::MAGNETIC_FIELD_RAW, SDK::Sensor::Type::MAGNETIC_FIELD, kPart)
        , mBaro("baro", SDK::Sensor::Type::PRESSURE, SDK::Sensor::Type::ALTIMETER, kBaro)
        , mBattery("battery", SDK::Sensor::Type::BATTERY_METRICS, kSweep)
    {
    }

    void run()
    {
        LOG_INFO("=== Sensor source step test ===\n");

        mFailures = 0;

        runSource(mAccel);
        runSource(mGyro);
        runSource(mFusion);
        runSource(mMag);
        runSource(mBaro);
        runSource(mBattery);

        LOG_INFO("Sensor source step test completed: %lu failure(s)\n", static_cast<unsigned long>(mFailures));
    }

private:
    /// The periods a source is asked for, each at least twice the one
    /// before, and how long it is given to take one and hand samples over
    /// at it.
    struct Timing {
        uint32_t fastMs;
        uint32_t midMs;
        uint32_t slowMs;
        uint32_t latencyMs;     ///< 0 for the period itself
        uint32_t settleMs;
    };

    // A part with rates of its own: 100, 50 and 25 Hz, which the
    // accelerometer, the gyroscope and the magnetometer all offer.
    static constexpr Timing kPart  { 10, 20, 40, 200, 1500 };
    // The barometer, on three rungs of its ladder. Slowing down, it
    // measures one new period after its last measurement.
    static constexpr Timing kBaro  { 100, 200, 1000, 0, 2500 };
    // A source sampled on the sweep. It takes a new period up at its next
    // sweep, which can be a whole old period away, and gives its first
    // sample one new period after that: 2 + 4 s at the longest here.
    static constexpr Timing kSweep { 1000, 2000, 4000, 0, 7000 };

    /// One source: two listeners of its channel and, where there is one, two
    /// of the processed channel, which is itself a listener of the first.
    struct Source {
        Source(const char* sourceName, SDK::Sensor::Type channelType, SDK::Sensor::Type processedType,
               const Timing& timing)
            : name(sourceName)
            , channel(channelType)
            , hasProcessed(true)
            , t(timing)
            , a("A", channelType)
            , b("B", channelType)
            , procA("processed A", processedType)
            , procB("processed B", processedType)
        {
        }

        Source(const char* sourceName, SDK::Sensor::Type channelType, const Timing& timing)
            : name(sourceName)
            , channel(channelType)
            , hasProcessed(false)
            , t(timing)
            , a("A", channelType)
            , b("B", channelType)
            , procA("unused", channelType)
            , procB("unused", channelType)
        {
        }

        const char*       name;
        SDK::Sensor::Type channel;
        bool              hasProcessed;
        Timing            t;
        Probe             a;
        Probe             b;
        Probe             procA;
        Probe             procB;
    };

    void runSource(Source& s)
    {
        soleListener(s);
        fastestOfTwo(s);
        notTheFastest(s);

        if (s.hasProcessed) {
            processedChain(s);
        }

        samePeriod(s);
    }

    /**
     * S1. The only listener changes its period on the subscription it holds:
     *     the source follows it both ways.
     */
    void soleListener(Source& s)
    {
        s.a.connect(s.t.midMs, s.t.latencyMs);
        expect("S1", s, s.a, s.t.midMs, "one listener at %lu ms", s.t.midMs);

        s.a.connect(s.t.fastMs, s.t.latencyMs);
        expect("S1", s, s.a, s.t.fastMs, "it asks for %lu ms", s.t.fastMs);

        s.a.connect(s.t.midMs, s.t.latencyMs);
        expect("S1", s, s.a, s.t.midMs, "it asks for %lu ms", s.t.midMs);

        s.a.connect(s.t.slowMs, s.t.latencyMs);
        expect("S1", s, s.a, s.t.slowMs, "it asks for %lu ms", s.t.slowMs);

        s.a.disconnect();
        OS::Delay(500);
    }

    /**
     * S2. The faster of two listeners slows down past the other: the source
     *     runs at the period of the one that is now the fastest.
     */
    void fastestOfTwo(Source& s)
    {
        s.a.connect(s.t.fastMs, s.t.latencyMs);
        s.b.connect(s.t.midMs, s.t.latencyMs);
        expect("S2", s, s.b, s.t.fastMs, "listeners at %lu and %lu ms", s.t.fastMs, s.t.midMs);

        s.a.connect(s.t.slowMs, s.t.latencyMs);
        expect("S2", s, s.b, s.t.midMs, "the %lu ms one asks for %lu ms", s.t.fastMs, s.t.slowMs);

        s.a.disconnect();
        s.b.disconnect();
        OS::Delay(500);
    }

    /**
     * S3. The slower of two listeners slows down further: the source keeps
     *     the period of the faster one.
     */
    void notTheFastest(Source& s)
    {
        s.a.connect(s.t.fastMs, s.t.latencyMs);
        s.b.connect(s.t.midMs, s.t.latencyMs);
        expect("S3", s, s.a, s.t.fastMs, "listeners at %lu and %lu ms", s.t.fastMs, s.t.midMs);

        s.b.connect(s.t.slowMs, s.t.latencyMs);
        expect("S3", s, s.a, s.t.fastMs, "the %lu ms one asks for %lu ms", s.t.midMs, s.t.slowMs);

        s.a.disconnect();
        s.b.disconnect();
        OS::Delay(500);
    }

    /**
     * S4. Listeners of the processed channel, which holds one subscription
     *     to the source's channel and passes its own shortest period up on
     *     it: the source slows down when the faster of them leaves, and when
     *     the one left asks for a longer period.
     */
    void processedChain(Source& s)
    {
        s.procA.connect(s.t.fastMs, s.t.latencyMs);
        s.procB.connect(s.t.midMs, s.t.latencyMs);
        expect("S4", s, s.procB, s.t.fastMs, "processed listeners at %lu and %lu ms", s.t.fastMs, s.t.midMs);

        s.procA.disconnect();
        expect("S4", s, s.procB, s.t.midMs, "the %lu ms one leaves", s.t.fastMs);

        s.procB.connect(s.t.slowMs, s.t.latencyMs);
        expect("S4", s, s.procB, s.t.slowMs, "the one left asks for %lu ms", s.t.slowMs);

        s.procB.disconnect();
        OS::Delay(500);
    }

    /**
     * S5. Two listeners that come to ask for the same period. The faster
     *     slows to the period of the other; then one of the two slows
     *     further, which must change nothing while the other stays; then the
     *     other follows it.
     */
    void samePeriod(Source& s)
    {
        s.a.connect(s.t.fastMs, s.t.latencyMs);
        s.b.connect(s.t.midMs, s.t.latencyMs);
        expect("S5", s, s.b, s.t.fastMs, "listeners at %lu and %lu ms", s.t.fastMs, s.t.midMs);

        s.a.connect(s.t.midMs, s.t.latencyMs);
        expect("S5", s, s.b, s.t.midMs, "the %lu ms one asks for %lu ms", s.t.fastMs, s.t.midMs);

        s.a.connect(s.t.slowMs, s.t.latencyMs);
        expect("S5", s, s.b, s.t.midMs, "one of the two at %lu ms asks for %lu ms", s.t.midMs, s.t.slowMs);

        s.b.connect(s.t.slowMs, s.t.latencyMs);
        expect("S5", s, s.b, s.t.slowMs, "the other asks for %lu ms too", s.t.slowMs);

        s.a.disconnect();
        s.b.disconnect();
        OS::Delay(500);
    }

    /**
     * @brief Let a change settle, then judge the step the source reports.
     *
     * @param fed      A listener that stays connected through the change: no
     *                 samples for it means the step read is not a fresh one.
     * @param periodMs Period the source is expected to run at.
     * @param what     The change, as a format for up to two periods in ms.
     */
    void expect(const char* scenario, Source& s, Probe& fed, uint32_t periodMs,
                const char* what, uint32_t first, uint32_t second = 0)
    {
        fed.restartAll(0, 0);
        OS::Delay(s.t.settleMs);

        const Sensor::Driver* driver = Sensor::Manager::getInstance().getDefaultSensor(s.channel);

        const uint32_t stepUs   = (driver != nullptr) ? driver->getSourceStepUs() : 0;
        const uint32_t samples  = fed.get().samples;
        const uint32_t expected = periodMs * 1000u;

        // The periods asked for are a factor of two apart or more, so a
        // tenth either way tells them apart whatever a part calls its
        // nominal step.
        // Compared in 64 bits, so that no step a source can report wraps.
        const bool ok = (samples != 0)
                     && (static_cast<uint64_t>(stepUs) * 10u >= static_cast<uint64_t>(expected) * 9u)
                     && (static_cast<uint64_t>(stepUs) * 10u <= static_cast<uint64_t>(expected) * 11u);

        char change[64];
        snprintf(change, sizeof(change), what, static_cast<unsigned long>(first), static_cast<unsigned long>(second));

        LOG_INFO("[%s %s] %s: source step %lu us, expected %lu, %lu sample(s) -> %s\n",
                 scenario,
                 s.name,
                 change,
                 static_cast<unsigned long>(stepUs),
                 static_cast<unsigned long>(expected),
                 static_cast<unsigned long>(samples),
                 ok ? "PASS" : "FAIL");

        if (!ok) {
            ++mFailures;
        }
    }

    Source   mAccel;
    Source   mGyro;
    Source   mFusion;
    Source   mMag;
    Source   mBaro;
    Source   mBattery;
    uint32_t mFailures = 0;
};

void TestSensorSourceStep_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    // Static: the probes of all the sources are more than the stack should carry.
    static TestSensorSourceStep test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
