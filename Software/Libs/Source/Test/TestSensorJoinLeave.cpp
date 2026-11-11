/**
 ******************************************************************************
 * @file    TestSensorJoinLeave.cpp
 * @brief   On-device test of what a listener is given while a faster one
 *          joins its source and leaves it
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSensorJoinLeave"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestSensorJoinLeave.hpp"
#include "Test/SensorProbe.hpp"
#include "OS.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

#include <cstdint>

/**
 * @brief How far apart a listener's samples are while its source changes rate.
 *
 * A source runs at the shortest period any listener asks for, so a faster
 * listener joining or leaving changes the step under a slower one that stays.
 * The slower one is to go on getting a sample a period: none out of turn as
 * the faster one joins or leaves. What the leave costs it is the source's own
 * doing: a source starts its slower step from where it was, so the next
 * sample can be most of two periods after the last one the listener had.
 *
 * Judged is every step between two samples of the slow listener, by their
 * stamps, in three stretches: alone, across the join and across the leave.
 * The change is made a set part of a period after a sample of the slow
 * listener, so a sample handed over at once on the change shows as a step
 * of that part of a period, each time. Not so on the paired IMU stream: its
 * slow period is shorter than the IMU's FIFO is read at, so the part of a
 * period is whatever the read makes it. Keep the watch still.
 *
 * The paired IMU stream fails a pass now and then on a stamp that goes back:
 * the IMU stamps its samples by software and starts over on a change of
 * rate, and the first stamp after it can come out a millisecond behind the
 * last one before.
 */
class TestSensorJoinLeave
{
public:
    TestSensorJoinLeave()
        : mAccelRaw("accel raw", SDK::Sensor::Type::ACCELEROMETER_RAW, kPart)
        , mAccel("accel", SDK::Sensor::Type::ACCELEROMETER, kPart)
        , mAccelBatched("accel, batched", SDK::Sensor::Type::ACCELEROMETER, kPartBatched)
        , mGyroRaw("gyro raw", SDK::Sensor::Type::GYROSCOPE_RAW, kPart)
        , mFusionRaw("fusion raw", SDK::Sensor::Type::FUSION_RAW, kFusion)
        , mMagRaw("mag raw", SDK::Sensor::Type::MAGNETIC_FIELD_RAW, kPart)
        , mPressure("pressure", SDK::Sensor::Type::PRESSURE, kBaro)
        , mAltimeter("altimeter", SDK::Sensor::Type::ALTIMETER, kBaro)
        , mBattery("battery", SDK::Sensor::Type::BATTERY_METRICS, kSweep)
    {
    }

    void run()
    {
        LOG_INFO("=== Sensor join and leave test ===\n");

        mFailures = 0;

        runSource(mAccelRaw);
        runSource(mAccel);
        runSource(mAccelBatched);
        runSource(mGyroRaw);
        runSource(mFusionRaw);
        runSource(mMagRaw);
        runSource(mPressure);
        runSource(mAltimeter);
        runSource(mBattery);

        LOG_INFO("Sensor join and leave test completed: %lu failure(s)\n", static_cast<unsigned long>(mFailures));
    }

private:
    /// The periods of the listener that stays and of the one that comes and
    /// goes. Both are rates the source has, so the one that stays is given
    /// the same period at either.
    struct Timing {
        uint32_t slowMs;
        uint32_t fastMs;
        uint32_t latencyMs;         ///< Of both listeners; 0 for the period itself
    };

    // A part with rates of its own: the accelerometer, the gyroscope and the
    // magnetometer all have 100 Hz, and 320 ms is a whole number of steps of
    // every rate of theirs that is faster.
    static constexpr Timing kPart   { 320, 10, 0 };
    // The same with both listeners taking their samples four seconds at a
    // time. The IMU is read every second at the least, so the processed
    // channel holds fast samples in its queue on the raw one for seconds,
    // and has them there when the fast listener leaves. The change comes
    // when it comes: a batched listener's samples do not tell the instant.
    static constexpr Timing kPartBatched { 320, 10, 4000 };
    // The two streams of the IMU paired, which run no slower than the
    // gyroscope does.
    static constexpr Timing kFusion { 40, 10, 0 };
    // The barometer, on two rungs of its ladder.
    static constexpr Timing kBaro   { 1000, 100, 0 };
    // A source sampled on the sweep.
    static constexpr Timing kSweep  { 4000, 1000, 0 };

    /// The shortest and the longest step of the slow listener, us.
    struct Bounds {
        uint32_t minUs;
        uint32_t maxUs;
    };

    /// A twentieth of the slow period, for a part's own clock; a tenth
    /// where the samples are taken a batch at a time, the IMU then spreading
    /// its stamps by software over a second of readings.
    static uint32_t slackUs(const Timing& t)
    {
        return t.slowMs * ((t.latencyMs != 0) ? 100u : 50u);
    }

    /// On a steady stream: a period.
    static Bounds steady(const Timing& t)
    {
        return { t.slowMs * 1000u - slackUs(t), t.slowMs * 1000u + slackUs(t) };
    }

    /// Across a join: the sample nearest a period after the last, which is
    /// within half a step of the faster rate - or a whole one later, where
    /// the source gives no sample at the instant it takes the faster rate up.
    static Bounds join(const Timing& t)
    {
        return { t.slowMs * 1000u - t.fastMs * 500u - slackUs(t), t.slowMs * 1000u + t.fastMs * 1000u + slackUs(t) };
    }

    /// Across a leave: no sample under half a period after the last, and the
    /// first one of the slower step, which the source gives a period after
    /// it took the rate up; that is after the last fast sample, or after the
    /// next sweep.
    static Bounds leave(const Timing& t)
    {
        return { t.slowMs * 500u - t.fastMs * 500u - slackUs(t), t.slowMs * 2000u + t.fastMs * 1000u + slackUs(t) };
    }

    static constexpr uint32_t kRounds = 3;

    struct Source {
        Source(const char* sourceName, SDK::Sensor::Type type, const Timing& timing)
            : name(sourceName)
            , t(timing)
            , slow("slow", type)
            , fast("fast", type)
        {
        }

        const char* name;
        Timing      t;
        Probe       slow;
        Probe       fast;
    };

    void runSource(Source& s)
    {
        // A stretch long enough for a listener to be handed what it covers.
        const uint32_t settleMs = 2 * s.t.latencyMs;

        if (!s.slow.connect(s.t.slowMs, s.t.latencyMs)) {
            LOG_INFO("[%s] not connected -> FAIL\n", s.name);
            ++mFailures;
            return;
        }

        // The stream of a first connection, and the samples it starts with.
        OS::Delay(kStartMs + 3 * s.t.slowMs + settleMs);

        s.slow.traceShortGaps(true);
        s.slow.restartAll(s.t.slowMs, 0);
        OS::Delay(4 * s.t.slowMs + settleMs);
        judge(s, "alone", 0, steady(s.t));

        for (uint32_t round = 1; round <= kRounds; ++round) {
            // A quarter, a half and three quarters of a period after a
            // sample of the slow listener.
            const uint32_t afterMs = (s.t.slowMs * round) / (kRounds + 1);

            if (!afterSample(s, afterMs)) {
                continue;
            }

            s.slow.restart(s.t.slowMs, 0);
            s.fast.restartAll(0, 0);

            const bool joined = s.fast.connect(s.t.fastMs, s.t.latencyMs);

            OS::Delay(3 * s.t.slowMs + settleMs);

            // A join that was refused, or gave the fast listener nothing or
            // nothing faster than the slow one has, leaves a steady stream
            // to judge. Some step of its own period shows it ran.
            const Probe::Snapshot fast = s.fast.get();
            const bool            ran  = (fast.steps != 0) && (fast.minStepUs <= s.t.fastMs * 1500u);

            if (!joined || !ran) {
                LOG_INFO("[%s] the fast listener %s -> FAIL\n", s.name,
                         !joined ? "was refused" : ((fast.steps == 0) ? "got nothing" : "ran no faster"));
                ++mFailures;
            }

            judge(s, "join", afterMs, join(s.t));

            if (!afterSample(s, afterMs)) {
                s.fast.disconnect();
                continue;
            }

            s.slow.restart(s.t.slowMs, 0);
            s.fast.disconnect();
            OS::Delay(4 * s.t.slowMs + settleMs);
            judge(s, "leave", afterMs, leave(s.t));
        }

        s.slow.disconnect();
        OS::Delay(500);
    }

    /**
     * @brief Wait for the next sample of the slow listener, and then this
     *        much longer.
     * @return false when no sample came in three periods.
     */
    bool afterSample(Source& s, uint32_t afterMs)
    {
        if (s.t.latencyMs != 0) {
            // Handed over a batch at a time: no sample to wait for.
            OS::Delay(afterMs);
            return true;
        }

        const uint32_t before = s.slow.get().samples;

        for (uint32_t waited = 0; waited < 3 * s.t.slowMs; waited += kPollMs) {
            if (s.slow.get().samples != before) {
                OS::Delay(afterMs);
                return true;
            }

            OS::Delay(kPollMs);
        }

        LOG_INFO("[%s] no sample in %lu ms -> FAIL\n", s.name, static_cast<unsigned long>(3 * s.t.slowMs));
        ++mFailures;

        return false;
    }

    /**
     * @brief Judge the steps of the slow listener since its last restart.
     * @param afterMs How long after a sample of the slow listener the change
     *                was made, 0 for none.
     */
    void judge(Source& s, const char* stretch, uint32_t afterMs, const Bounds& bounds)
    {
        const Probe::Snapshot got = s.slow.get();

        const bool ok = (got.steps != 0)
                     && (got.minStepUs >= bounds.minUs) && (got.maxStepUs <= bounds.maxUs)
                     && (got.zeroStamps == 0) && (got.duplicates == 0) && (got.backwards == 0);

        LOG_INFO("[%s] %s at +%lu ms: %lu step(s) of %lu..%lu us, wanted %lu..%lu, "
                 "zero %lu, twice %lu, back %lu -> %s\n",
                 s.name,
                 stretch,
                 static_cast<unsigned long>(afterMs),
                 static_cast<unsigned long>(got.steps),
                 static_cast<unsigned long>(got.minStepUs),
                 static_cast<unsigned long>(got.maxStepUs),
                 static_cast<unsigned long>(bounds.minUs),
                 static_cast<unsigned long>(bounds.maxUs),
                 static_cast<unsigned long>(got.zeroStamps),
                 static_cast<unsigned long>(got.duplicates),
                 static_cast<unsigned long>(got.backwards),
                 ok ? "PASS" : "FAIL");

        if (ok) {
            return;
        }

        ++mFailures;

        // The deliveries around the first one that came early or late. The
        // slow listener's queue is one sample unless it is batched, so each
        // is a sample.
        Probe::Delivery trace[Probe::kTraceLength];
        const uint32_t  held = s.slow.trace(trace);

        for (uint32_t i = 0; i < held; ++i) {
            LOG_INFO("[%s]   at %lu (+%lu): %u sample(s), stamped %lu..%lu\n",
                     s.name,
                     static_cast<unsigned long>(trace[i].atMs),
                     static_cast<unsigned long>((i != 0) ? trace[i].atMs - trace[i - 1].atMs : 0),
                     static_cast<unsigned>(trace[i].count),
                     static_cast<unsigned long>(trace[i].firstMs),
                     static_cast<unsigned long>(trace[i].lastMs));
        }
    }

    static constexpr uint32_t kPollMs  = 5;
    /// What a stream is given to start in, on top of its own periods.
    static constexpr uint32_t kStartMs = 1500;

    Source   mAccelRaw;
    Source   mAccel;
    Source   mAccelBatched;
    Source   mGyroRaw;
    Source   mFusionRaw;
    Source   mMagRaw;
    Source   mPressure;
    Source   mAltimeter;
    Source   mBattery;
    uint32_t mFailures = 0;
};

void TestSensorJoinLeave_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    // Static: the probes of all the sources are more than the stack should carry.
    static TestSensorJoinLeave test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
