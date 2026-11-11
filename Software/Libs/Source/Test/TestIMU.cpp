#define LOG_MODULE_PRX      "TestIMU"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestIMU.hpp"
#include "Hardware/BMI270/BMI270.hpp"
#include "Gpio.hpp"
#include "System.hpp"
#include "Instances/InstanceI2C.hpp"
#include "OS.hpp"
#include "ComponentFactory.hpp"
#include "IImu.hpp"
#include "ImuFifoCadence.hpp"
#include "ImuTimeBase.hpp"

#include <cassert>
#include <cstdio>

/// Data period of a rate in whole Hz, the way ImuBosch is asked for it.
static constexpr uint32_t hz(uint32_t rate)
{
    return 1000000u / rate;
}

/**
 * @brief What one stream delivered over a stretch of time.
 *
 * Filled from the IMU worker thread, one call per FIFO read, and read from
 * the test thread. Two things are measured apart. Cadence - how often the
 * FIFO is read and how much a read carries - starts over with every
 * scenario and may skip the reads a change takes to settle. Continuity -
 * whether the timestamps run on from one read into the next - is kept across
 * scenarios until it is dropped on purpose.
 */
class StreamStats
{
public:
    struct Snapshot {
        uint32_t reads         = 0;     ///< Reads counted for the cadence
        uint32_t frames        = 0;     ///< Frames in those reads
        uint32_t laterFrames   = 0;     ///< Frames in all of them but the first
        uint32_t minFrames     = 0;
        uint32_t maxFrames     = 0;
        uint32_t startMs       = 0;     ///< When the measurement started
        uint32_t firstReadMs   = 0;     ///< First read of any kind after that
        uint32_t firstCountMs  = 0;     ///< First read counted for the cadence
        uint32_t lastCountMs   = 0;
        uint32_t gaps          = 0;
        uint32_t sumGapMs      = 0;
        uint32_t minGapMs      = 0;
        uint32_t maxGapMs      = 0;
        uint32_t lateReads     = 0;     ///< Gaps over 5/4 of the expected one
        uint32_t minStepUs     = 0;     ///< Timestamp step between two frames of a read
        uint32_t maxStepUs     = 0;
        uint32_t joins         = 0;
        uint32_t minJoinUs     = 0;     ///< Timestamp step from read to read
        uint32_t maxJoinUs     = 0;
        uint32_t backwards     = 0;     ///< Reads that began before the last one ended
        uint32_t future        = 0;     ///< Reads whose newest stamp was ahead of the clock
        int32_t  minAgeMs      = 0;     ///< Age of the newest reading when handed over
        int32_t  maxAgeMs      = 0;
    };

    void init(const char* name)
    {
        mMutex.init(name);
    }

    /**
     * @brief Start a new measurement and keep the continuity.
     * @param skipReads     Reads left out of the cadence while a change settles.
     * @param expectedGapMs Gap a late read is judged against, 0 not to judge.
     */
    void restart(uint32_t skipReads, uint32_t expectedGapMs)
    {
        OS::MutexCS cs(mMutex);

        mData           = Snapshot {};
        mHaveStep       = false;
        mData.startMs   = Driver::System::GetTimeMs();
        mSeen           = 0;
        mSkip           = skipReads;
        mExpectedGapMs  = expectedGapMs;
        mHavePrevRead   = false;
    }

    /// Start a new measurement and a new run of timestamps.
    void restartAll(uint32_t skipReads, uint32_t expectedGapMs)
    {
        restart(skipReads, expectedGapMs);

        OS::MutexCS cs(mMutex);
        mHavePrevTs = false;
    }

    /// One batch, as it was handed over: anything with a timestamp.
    template <typename Frame>
    void add(const Frame* data, size_t count)
    {
        const uint32_t now = Driver::System::GetTimeMs();

        OS::MutexCS cs(mMutex);

        if (count == 0) {
            return;
        }

        const uint64_t firstTs = data[0].timestamp;
        const uint64_t lastTs  = data[count - 1].timestamp;

        ++mSeen;

        if (mSeen == 1) {
            mData.firstReadMs = now;
        }

        if (mHavePrevTs) {
            if (firstTs < mPrevLastTs) {
                ++mData.backwards;
            } else {
                track(static_cast<uint32_t>(firstTs - mPrevLastTs), mData.joins == 0,
                      mData.minJoinUs, mData.maxJoinUs);
                ++mData.joins;
            }
        }

        mPrevLastTs = lastTs;
        mHavePrevTs = true;

        if (mSeen <= mSkip) {
            return;
        }

        const bool firstCounted = (mData.reads == 0);

        // Every step of the batch: frames set right at its two ends and
        // anywhere in between would not show in its length alone. A frame
        // not after the one before it counts as a step of nothing.
        for (size_t i = 1; i < count; ++i) {
            const uint64_t from = data[i - 1].timestamp;
            const uint64_t to   = data[i].timestamp;
            const uint32_t step = (to > from) ? static_cast<uint32_t>(to - from) : 0;

            track(step, !mHaveStep, mData.minStepUs, mData.maxStepUs);
            mHaveStep = true;
        }

        const int32_t age = static_cast<int32_t>(now - static_cast<uint32_t>(lastTs / 1000));

        // Judged in microseconds, the way the strictest consumer would.
        if (lastTs > static_cast<uint64_t>(now) * 1000ull) {
            ++mData.future;
        }
        if (firstCounted || age < mData.minAgeMs) {
            mData.minAgeMs = age;
        }
        if (firstCounted || age > mData.maxAgeMs) {
            mData.maxAgeMs = age;
        }

        track(static_cast<uint32_t>(count), firstCounted, mData.minFrames, mData.maxFrames);

        if (firstCounted) {
            mData.firstCountMs = now;
        } else {
            mData.laterFrames += static_cast<uint32_t>(count);
        }

        if (mHavePrevRead) {
            const uint32_t gap = now - mPrevReadMs;

            track(gap, mData.gaps == 0, mData.minGapMs, mData.maxGapMs);
            mData.sumGapMs += gap;
            ++mData.gaps;

            if (mExpectedGapMs != 0 && gap * 4 > mExpectedGapMs * 5) {
                ++mData.lateReads;
            }
        }

        mPrevReadMs      = now;
        mHavePrevRead    = true;
        mData.lastCountMs = now;
        mData.frames    += static_cast<uint32_t>(count);
        ++mData.reads;
    }

    Snapshot get()
    {
        OS::MutexCS cs(mMutex);
        return mData;
    }

private:
    static void track(uint32_t value, bool first, uint32_t& lo, uint32_t& hi)
    {
        if (first || value < lo) {
            lo = value;
        }
        if (first || value > hi) {
            hi = value;
        }
    }

    OS::Mutex mMutex;
    Snapshot  mData;
    uint32_t  mSeen          = 0;
    uint32_t  mSkip          = 0;
    uint32_t  mExpectedGapMs = 0;
    uint32_t  mPrevReadMs    = 0;
    bool      mHavePrevRead  = false;
    uint64_t  mPrevLastTs    = 0;
    bool      mHavePrevTs    = false;
    bool      mHaveStep      = false;
};


class TestIMU2 : public Interface::IImu::SensorCallback
{
public:
    TestIMU2(Interface::IImu& imu) : mImu(imu) {}

    /**
     * @brief Run accelerometer test.
     * Tests basic accelerometer functionality with FIFO.
     */
    void run_accel()
    {
        LOG_INFO("=== Accelerometer Test ===\n");

        mImu.setSensorCallback(this);
        mImu.setAccelRange(2);

        // Enable accelerometer: 100Hz
        if (!mImu.enableAccel(hz(100))) {
            LOG_ERROR("Failed to enable accelerometer\n");
            return;
        }

        LOG_INFO("Accelerometer enabled. Collecting data for 10 seconds...\n");
        OS::Delay(10000);

        mImu.disableAccel();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Accelerometer test completed\n");
    }

    /**
     * @brief Run gyroscope test.
     * Tests basic gyroscope functionality with FIFO.
     */
    void run_gyro()
    {
        LOG_INFO("=== Gyroscope Test ===\n");

        mImu.setSensorCallback(this);
        mImu.setGyroRange(250);

        // Enable gyroscope: 100Hz
        if (!mImu.enableGyro(hz(100))) {
            LOG_ERROR("Failed to enable gyroscope\n");
            return;
        }

        LOG_INFO("Gyroscope enabled. Collecting data for 10 seconds...\n");
        OS::Delay(10000);

        mImu.disableGyro();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Gyroscope test completed\n");
    }

    /**
     * @brief Run accelerometer with dynamic ODR change test.
     * Tests changing ODR on the fly.
     */
    void run_accel_dyn_odr()
    {
        LOG_INFO("=== Accelerometer Dynamic ODR Test ===\n");

        mImu.setSensorCallback(this);

        // Start with 50Hz
        LOG_INFO("Starting at 50Hz...\n");
        mImu.enableAccel(hz(50));
        OS::Delay(5000);

        // Change to 100Hz
        LOG_INFO("Changing to 100Hz...\n");
        mImu.enableAccel(hz(100));
        OS::Delay(5000);

        // Change to 200Hz
        LOG_INFO("Changing to 200Hz...\n");
        mImu.enableAccel(hz(200));
        OS::Delay(5000);

        // Change to 400Hz
        LOG_INFO("Changing to 400Hz...\n");
        mImu.enableAccel(hz(400));
        OS::Delay(5000);

        // Change to 800Hz
        LOG_INFO("Changing to 800Hz...\n");
        mImu.enableAccel(hz(800));
        OS::Delay(5000);

        // Change to 1600Hz
        LOG_INFO("Changing to 1600Hz...\n");
        mImu.enableAccel(hz(1600));
        OS::Delay(5000);

        mImu.disableAccel();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Dynamic ODR test completed\n");
    }

    /**
     * @brief Run accelerometer and gyroscope with dynamic ODR change.
     * Tests both sensors simultaneously with ODR changes.
     */
    void run_accel_gyro_dyn_odr()
    {
        LOG_INFO("=== Accel+Gyro Dynamic ODR Test ===\n");

        mImu.setSensorCallback(this);

        // Start both at 100Hz
        LOG_INFO("Starting both at 100Hz...\n");
        mImu.enableAccel(hz(100));
        mImu.enableGyro(hz(100));
        OS::Delay(5000);

        // Change accel to 200Hz
        LOG_INFO("Changing accel to 200Hz...\n");
        mImu.enableAccel(hz(200));
        OS::Delay(5000);

        // Change gyro to 400Hz
        LOG_INFO("Changing gyro to 400Hz...\n");
        mImu.enableGyro(hz(400));
        OS::Delay(5000);

        // Change accel to 1600Hz
        LOG_INFO("Changing accel to 1600Hz...\n");
        mImu.enableAccel(hz(1600));
        OS::Delay(5000);

        // Change gyro to 200Hz
        LOG_INFO("Changing gyro to 200Hz...\n");
        mImu.enableGyro(hz(200));
        OS::Delay(5000);

        // Disable accel
        LOG_INFO("Disable accel...\n");
        mImu.disableAccel();
        OS::Delay(5000);

        mImu.disableAll();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Accel+Gyro dynamic ODR test completed\n");
    }

    /**
     * @brief Run step counter test.
     * Tests step counting functionality.
     */
    void run_step_counter()
    {
        LOG_INFO("=== Step Counter Test ===\n");

        mImu.setSensorCallback(this);

        if (!mImu.enableStepCounter()) {
            LOG_ERROR("Failed to enable step counter\n");
            return;
        }

        LOG_INFO("Step counter enabled. Walk around for 30 seconds...\n");
        OS::Delay(30000);

        mImu.disableStepCounter();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Step counter test completed\n");
    }

    /**
     * @brief Run activity recognition test.
     * Tests activity detection (still/walking/running).
     */
    void run_activity_recognition()
    {
        LOG_INFO("=== Activity Recognition Test ===\n");

        mImu.setSensorCallback(this);

        if (!mImu.enableActivity()) {
            LOG_ERROR("Failed to enable activity recognition\n");
            return;
        }

        LOG_INFO("Activity recognition enabled.\n");
        LOG_INFO("Try: standing still, walking, running for 60 seconds...\n");
        OS::Delay(60000);

        mImu.disableActivity();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Activity recognition test completed\n");
    }

    /**
     * @brief Run motion interrupts test.
     * Tests any motion, no motion, and significant motion detection.
     */
    void run_motion_interrupts()
    {
        LOG_INFO("=== Motion Interrupts Test ===\n");

        mImu.setSensorCallback(this);

        // Enable all motion detectors
        LOG_INFO("Enabling any motion detector...\n");
        mImu.enableAnyMotion();

        LOG_INFO("Enabling no motion detector...\n");
        mImu.enableNoMotion();

        LOG_INFO("Enabling significant motion detector...\n");
        mImu.enableSigMotion();

        LOG_INFO("Motion detectors enabled.\n");
        LOG_INFO("Try: moving device, keeping it still, shaking it for 30 seconds...\n");
        OS::Delay(30000);

        mImu.disableAnyMotion();
        mImu.disableNoMotion();
        mImu.disableSigMotion();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Motion interrupts test completed\n");
    }

    /**
     * @brief Run wrist wakeup test.
     * Tests wrist wear wakeup gesture detection.
     */
    void run_wrist_wakeup()
    {
        LOG_INFO("=== Wrist Wakeup Test ===\n");

        mImu.setSensorCallback(this);

        if (!mImu.enableWristWakeup()) {
            LOG_ERROR("Failed to enable wrist wakeup\n");
            return;
        }

        LOG_INFO("Wrist wakeup enabled.\n");
        LOG_INFO("Try rotating wrist like checking a watch for 30 seconds...\n");
        OS::Delay(30000);

        mImu.disableWristWakeup();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Wrist wakeup test completed\n");
    }

    /**
     * @brief Run accelerometer and features test.
     * Tests basic accelerometer functionality with FIFO.
     */
    void run_accel_features()
    {
        LOG_INFO("=== Accelerometer and features Test ===\n");

        mImu.setSensorCallback(this);


//        LOG_INFO("Step counter\n");
//        mImu.enableStepCounter();
//
//        OS::Delay(30000);

//        LOG_INFO("Step detector\n");
//        mImu.enableStepDetector();
//
//        OS::Delay(30000);

//        LOG_INFO("Any motion detector\n");
//        mImu.enableAnyMotion();
//
//        OS::Delay(30000);
//
//        LOG_INFO("No motion detector\n");
//        mImu.enableNoMotion();
//
//        OS::Delay(30000);
//
//        LOG_INFO("Significant motion detector\n");
//        mImu.enableSigMotion();
//
//        OS::Delay(30000);
//
//        LOG_INFO("Wrist wakeup\n");
//        mImu.enableWristWakeup();

//        OS::Delay(30000);

        LOG_INFO("Accelerometer 200Hz\n");
        mImu.enableAccel(hz(200));

        OS::Delay(5000);

        LOG_INFO("Accelerometer 1600Hz\n");
        mImu.enableAccel(hz(1600));

        OS::Delay(5000);

        LOG_INFO("Any motion detector\n");
        mImu.enableAnyMotion();

        OS::Delay(5000);

        LOG_INFO("Accelerometer 400Hz\n");
        mImu.enableAccel(hz(400));

        OS::Delay(5000);

        LOG_INFO("Accelerometer disabled\n");
        mImu.disableAccel();

        OS::Delay(5000);
        LOG_INFO("Step detector\n");
        mImu.enableStepDetector();

        OS::Delay(5000);

        LOG_INFO("Any motion disabled\n");
        mImu.disableAnyMotion();

        OS::Delay(30000);

        LOG_INFO("End\n");

        mImu.disableAll();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Accelerometer and features test completed\n");
    }

    /**
     * @brief Run accel and gyro in sync mode.
     */
    void run_sync()
    {
        LOG_INFO("=== Sync Test ===\n");

        mImu.setSensorCallback(this);


        LOG_INFO("Accelerometer 100Hz\n");
        mImu.enableAccel(hz(100));

        OS::Delay(10000);

        LOG_INFO("Accel+Gyro 200Hz\n");
        mImu.enableAccel(hz(200));
        mImu.enableGyro(hz(200));

        OS::Delay(10000);

        LOG_INFO("Accelerometer disabled\n");
        mImu.disableAccel();

        OS::Delay(10000);

        mImu.disableAll();
        mImu.setSensorCallback(nullptr);

        LOG_INFO("Sync test completed\n");
    }

    /**
     * @brief FIFO read cadence against what the streams ask for.
     *
     * Every scenario measures how often the FIFO is read, how many frames a
     * read carries and whether the timestamps run on, prints one line and
     * says PASS or FAIL. Keep the watch still: nothing here needs motion, and
     * the motion features are only switched, never waited for.
     */
    void run_fifo_latency()
    {
        LOG_INFO("=== FIFO latency Test ===\n");

        mAccelStats.init("TestIMU.XL");
        mGyroStats.init("TestIMU.GY");
        mSyncStats.init("TestIMU.SY");

        mFailures = 0;
        mQuiet    = true;
        mImu.setSensorCallback(this);
        mImu.setAccelRange(8);
        mImu.setGyroRange(2000);

        const Expect kSecond25  { 900, 1100, 22, 28, ageLimitMs(175) };
        const Expect kSecond100 { 900, 1100, 90, 110, ageLimitMs(1300) };
        const Expect kShort25   { 70, 95, 2, 3, ageLimitMs(14) };
        const Expect kShort50   { 90, 110, 4, 6, ageLimitMs(35) };
        const Expect kShort100  { 90, 110, 9, 11, ageLimitMs(130) };

        // 1. No request: the FIFO is read about once a second.
        mImu.enableAccel(hz(25));
        mAccelStats.restartAll(1, 1000);
        OS::Delay(6500);
        check("1 default 25Hz", mAccelStats, kSecond25, 40000);

        // 2. A request on a running stream. The level moves, the timestamps
        //    run on: the join below covers the read that straddles the change.
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 100);
        mAccelStats.restart(3, 80);
        OS::Delay(5000);
        check("2 request 100ms", mAccelStats, kShort25, 40000);

        // 3. A request under the floor is held to the floor.
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 20);
        mAccelStats.restart(3, 80);
        OS::Delay(5000);
        check("3 request 20ms", mAccelStats, kShort25, 40000);

        // 4. The request withdrawn.
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 0);
        mAccelStats.restart(2, 1000);
        OS::Delay(7500);
        check("4 withdrawn", mAccelStats, kSecond25, 40000);

        // 5. The request made before the stream is enabled: the first read
        //    must not take a second.
        mImu.disableAccel();
        OS::Delay(500);
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 100);
        mAccelStats.restartAll(0, 0);
        mImu.enableAccel(hz(25));
        OS::Delay(3000);
        {
            const StreamStats::Snapshot s = mAccelStats.get();
            const uint32_t firstDelay = s.firstReadMs - s.startMs;
            const bool ok = (s.reads > 0) && (firstDelay < 400);

            LOG_INFO("[5 request first] first read after %lu ms -> %s\n",
                     static_cast<unsigned long>(firstDelay), ok ? "PASS" : "FAIL");
            if (!ok) {
                ++mFailures;
            }
        }
        mAccelStats.restart(0, 80);
        OS::Delay(4000);
        check("5 cadence", mAccelStats, kShort25, 40000);

        // 6. The level follows the data rate. The timestamps run on through
        //    a rate change, but the step from the last frame at the old rate
        //    to the first at the new one is neither period, so the steps are
        //    judged from a new start.
        mImu.enableAccel(hz(50));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(4000);
        check("6 50Hz", mAccelStats, kShort50, 20000);

        mImu.enableAccel(hz(100));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(4000);
        check("6 100Hz", mAccelStats, kShort100, 10000);

        // 7. One FIFO, two streams: the tighter request paces both, and a
        //    stream that is off does not count.
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 1000);
        mAccelStats.restart(2, 1000);
        OS::Delay(6500);
        check("7 accel 1000ms", mAccelStats, kSecond100, 10000);

        mImu.setFifoLatency(Interface::IImu::FifoChannel::GYRO, 100);
        mImu.enableGyro(hz(100));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        mGyroStats.restartAll(1, 100);
        mSyncStats.restartAll(1, 100);
        OS::Delay(4000);
        check("7 accel + gyro 100ms", mAccelStats, kShort100, 10000);
        check("7 gyro", mGyroStats, kShort100, 10000);
        check("7 sync", mSyncStats, kShort100, 10000);

        mImu.disableGyro();
        OS::Delay(1500);
        mAccelStats.restartAll(2, 1000);
        OS::Delay(6500);
        check("7 gyro off", mAccelStats, kSecond100, 10000);

        // 8. Features switched under a running stream. Neither touches the
        //    FIFO, so the timestamps must run on through every switch.
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 100);
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        for (int i = 0; i < 5; ++i) {
            mImu.enableNoMotion(false);
            OS::Delay(600);
            mImu.disableNoMotion();
            OS::Delay(600);
            mImu.enableStepCounter();
            OS::Delay(600);
            mImu.disableStepCounter();
            OS::Delay(600);
        }
        check("8 features switched", mAccelStats, kShort100, 10000);

        // 9. A minute at 100 Hz and 100 ms: how steady the step is, whether
        //    every frame arrives, how often a read is late.
        OS::Delay(1500);
        mAccelStats.restartAll(10, 100);
        OS::Delay(60000);
        check("9 one minute", mAccelStats, kShort100, 10000);
        {
            const StreamStats::Snapshot s = mAccelStats.get();
            const uint32_t spread   = s.maxStepUs - s.minStepUs;
            const uint32_t elapsed  = s.lastCountMs - s.firstCountMs;
            const uint32_t expected = elapsed / 10;     // 100 Hz
            const uint32_t diff     = (s.laterFrames > expected) ? s.laterFrames - expected
                                                                 : expected - s.laterFrames;
            // Three things move the step of a batch, and none of them is a
            // fault. The read times come from a millisecond clock, and a
            // batch is squeezed by up to a tick to stay behind its read: a
            // percent on ten frames. A correction comes in at under a percent
            // of the step. The measured step itself wanders by about half a
            // percent. Under 3 % is those three and nothing worse.
            const bool stepOk   = (spread * 100 < 3 * 10000);
            const bool framesOk = (diff * 50 <= expected);          // within 2 %

            LOG_INFO("[9 one minute] step spread %lu us -> %s, frames %lu of %lu -> %s, "
                     "late reads %lu of %lu, worst gap %lu ms\n",
                     static_cast<unsigned long>(spread), stepOk ? "PASS" : "FAIL",
                     static_cast<unsigned long>(s.laterFrames),
                     static_cast<unsigned long>(expected), framesOk ? "PASS" : "FAIL",
                     static_cast<unsigned long>(s.lateReads),
                     static_cast<unsigned long>(s.gaps),
                     static_cast<unsigned long>(s.maxGapMs));

            if (!stepOk) {
                ++mFailures;
            }
            if (!framesOk) {
                ++mFailures;
            }
        }

        // 10. The features need the accelerometer at 50 Hz or faster. A
        //     listener asking for 25 Hz gets 25 Hz alone, 50 Hz while a feature
        //     is on, and 25 Hz again when it goes; the watermark follows.
        mImu.enableAccel(hz(25));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 80);
        OS::Delay(3000);
        check("10 25Hz alone", mAccelStats, kShort25, 40000);

        mImu.enableStepCounter();
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(3000);
        check("10 feature on: 50Hz", mAccelStats, kShort50, 20000);

        mImu.disableStepCounter();
        OS::Delay(1500);
        mAccelStats.restartAll(1, 80);
        OS::Delay(3000);
        check("10 feature off: 25Hz", mAccelStats, kShort25, 40000);

        // 11. A request belongs to the run it was made in.
        mImu.disableAll();
        OS::Delay(500);
        mImu.deinit();
        mImu.init();
        mImu.enableAccel(hz(25));
        mAccelStats.restartAll(1, 1000);
        OS::Delay(6500);
        check("11 after restart", mAccelStats, kSecond25, 40000);

        mImu.disableAll();
        OS::Delay(500);
        mImu.setSensorCallback(nullptr);
        mQuiet = false;

        LOG_INFO("FIFO latency test completed: %lu failure(s)\n",
                 static_cast<unsigned long>(mFailures));
    }

    /**
     * @brief A stream that starts again minutes after it stopped, with the
     *        part kept on meanwhile.
     *
     * A feature keeps the part on, so its counter runs on while the stream
     * is off and has gone more than half of its range, which is 655 s, by
     * the time the stream comes back. The stream has to be handed over
     * from its first read all the same. Takes six minutes, so it is not in
     * the regular run.
     */
    void run_restart_after_pause()
    {
        LOG_INFO("=== Restart after a pause Test ===\n");

        mAccelStats.init("TestIMU.XL");
        mGyroStats.init("TestIMU.GY");
        mSyncStats.init("TestIMU.SY");

        mFailures = 0;
        mQuiet    = true;
        mImu.setSensorCallback(this);
        mImu.setAccelRange(8);

        const Expect kShort100 { 90, 110, 9, 11, ageLimitMs(130) };

        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 100);
        mImu.enableStepCounter();

        mImu.enableAccel(hz(100));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(3000);
        check("before the pause", mAccelStats, kShort100, 10000);

        mImu.disableAccel();
        OS::Delay(kPauseMs);

        mImu.enableAccel(hz(100));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(3000);
        check("after the pause", mAccelStats, kShort100, 10000);

        mImu.disableAll();
        OS::Delay(500);
        mImu.setSensorCallback(nullptr);
        mQuiet = false;

        LOG_INFO("Restart after a pause test completed: %lu failure(s)\n",
                 static_cast<unsigned long>(mFailures));
    }

    /**
     * @brief The frame times through the gyroscope being switched on and off.
     *
     * The part's counter, which the frame times come from, runs on another
     * oscillator while the gyroscope runs, more than a per cent off the
     * first. Once each has been seen, the frames must be evenly spaced right
     * after every switch: a rate found anew each time would show as steps
     * that differ by up to that per cent over the first reads. The read the
     * switch falls in holds frames of both, so it is let through before the
     * steps are judged; the times run on through it all the same.
     */
    void run_gyro_switch()
    {
        LOG_INFO("=== Gyroscope switch Test ===\n");

        mAccelStats.init("TestIMU.XL");
        mGyroStats.init("TestIMU.GY");
        mSyncStats.init("TestIMU.SY");

        mFailures = 0;
        mQuiet    = true;
        mImu.setSensorCallback(this);
        mImu.setAccelRange(8);
        mImu.setGyroRange(2000);

        const Expect kShort100 { 90, 110, 9, 11, ageLimitMs(130) };

        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 100);
        mImu.setFifoLatency(Interface::IImu::FifoChannel::GYRO, 100);

        // Each oscillator is seen once, long enough for its rate to be found.
        mImu.enableAccel(hz(100));
        OS::Delay(1500);
        mAccelStats.restartAll(1, 100);
        OS::Delay(6000);
        check("gyro never on", mAccelStats, kShort100, 10000);

        mImu.enableGyro(hz(100));
        OS::Delay(1500);
        mAccelStats.restart(1, 100);
        OS::Delay(6000);
        check("gyro on first", mAccelStats, kShort100, 10000);

        for (int i = 0; i < 3; ++i) {
            char name[32];

            mImu.disableGyro();
            OS::Delay(kSwitchSettleMs);
            mAccelStats.restart(0, 100);
            OS::Delay(3000);
            snprintf(name, sizeof(name), "gyro off %d", i + 1);
            checkEven(name, mAccelStats, 10000);

            mImu.enableGyro(hz(100));
            OS::Delay(kSwitchSettleMs);
            mAccelStats.restart(0, 100);
            mGyroStats.restartAll(0, 100);
            OS::Delay(3000);
            snprintf(name, sizeof(name), "gyro on %d", i + 1);
            checkEven(name, mAccelStats, 10000);
            snprintf(name, sizeof(name), "gyro on %d, gyro", i + 1);
            checkEven(name, mGyroStats, 10000);
        }

        mImu.disableAll();
        OS::Delay(500);
        mImu.setSensorCallback(nullptr);
        mQuiet = false;

        LOG_INFO("Gyroscope switch test completed: %lu failure(s)\n",
                 static_cast<unsigned long>(mFailures));
    }

    /**
     * @brief The FIFO cadence over data rates, stream combinations and requests.
     *
     * What each configuration should do is not written down here: it is
     * worked out from the same two functions the driver uses, the read period
     * the streams settle on and the watermark that period comes to. The part
     * then has to read at that pace and hand over that many frames. This
     * covers what the named scenarios leave out: the slowest and the fastest
     * rates, the watermark held to the limit of the part, the gyroscope alone,
     * and the two streams at different rates, where the FIFO mixes shared
     * frames with frames of the faster stream. The five slowest rates of the
     * part are fractions of a Hz, so the streams are given by their periods
     * and all the arithmetic is in periods.
     */
    void run_fifo_matrix()
    {
        LOG_INFO("=== FIFO cadence matrix ===\n");

        mAccelStats.init("TestIMU.XL");
        mGyroStats.init("TestIMU.GY");
        mSyncStats.init("TestIMU.SY");

        mFailures = 0;
        mQuiet    = true;
        mImu.setSensorCallback(this);
        mImu.setAccelRange(8);
        mImu.setGyroRange(2000);

        // All twelve rates of the accelerometer, 0.78125 Hz to 1600 Hz.
        static const uint32_t kAccelPeriods[] = { 1280000, 640000, 320000, 160000, 80000, 40000,
                                                  20000, 10000, 5000, 2500, 1250, 625 };
        static const uint32_t kRequests[]     = { 0, 100, 500 };

        for (uint32_t period : kAccelPeriods) {
            for (uint32_t request : kRequests) {
                runConfig(period, 0, request);
            }
        }

        // A period between two rates gets the faster one: 1000 ms is run at
        // 640 ms, 77 ms at 40 ms.
        runConfig(1000000, 0, 0, 640000, 0);
        runConfig(77000, 0, 100, 40000, 0);

        // From the fastest rate straight to the slowest: the first read after
        // the change is nowhere near the new rate's grid.
        runConfig(hz(1600), 0, 0);
        runConfig(1280000, 0, 0);

        runConfig(0, hz(25), 100);
        runConfig(0, hz(200), 100);
        runConfig(0, hz(1600), 100);
        runConfig(0, hz(1600), 0);

        // The gyroscope has no rate under 25 Hz.
        runConfig(0, 80000, 100, 0, 40000);

        runConfig(hz(100), hz(100), 0);
        runConfig(hz(100), hz(100), 100);
        runConfig(hz(400), hz(400), 0);
        runConfig(hz(400), hz(400), 100);
        runConfig(hz(1600), hz(1600), 0);
        runConfig(hz(1600), hz(1600), 100);

        runConfig(hz(50), hz(200), 100);
        runConfig(hz(400), hz(100), 100);
        runConfig(hz(1600), hz(25), 100);
        runConfig(hz(25), hz(1600), 0);

        // The heaviest load there is, held for a while: every frame must
        // arrive. A FIFO that fills up loses its oldest frames, which shows
        // here as frames missing.
        runConfig(hz(1600), hz(1600), 100);
        mAccelStats.restartAll(5, 0);
        mGyroStats.restartAll(5, 0);
        OS::Delay(20000);
        soak("accel 1600", mAccelStats, 1600);
        soak("gyro 1600", mGyroStats, 1600);

        mImu.disableAll();
        OS::Delay(500);
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, 0);
        mImu.setFifoLatency(Interface::IImu::FifoChannel::GYRO, 0);
        mImu.setSensorCallback(nullptr);
        mQuiet = false;

        LOG_INFO("FIFO cadence matrix completed: %lu failure(s)\n",
                 static_cast<unsigned long>(mFailures));
    }

private:
    /// What the bus takes over one byte of a long FIFO read, in us.
    static constexpr uint64_t kFifoByteUs = 11;

    /// From asking for the gyroscope to be switched until the read the
    /// switch falls in, and the one after it, have gone by.
    static constexpr uint32_t kSwitchSettleMs = 300;

    /// How far the age of a read's newest reading may move over a stretch
    /// whose times keep pace with the clock: what the hand-overs scatter by.
    /// Times half a per cent off the pace move it by 15 ms in three seconds.
    static constexpr int32_t kPaceMs = 8;

    /// A pause over half of the 655 s the part's counter takes to wrap.
    static constexpr uint32_t kPauseMs = 340000;

    /// What a scenario expects of the cadence, as averages over its reads.
    struct Expect {
        uint32_t gapMinMs;
        uint32_t gapMaxMs;
        uint32_t framesMin;
        uint32_t framesMax;
        uint32_t ageMaxMs;      ///< Oldest the newest reading may be when handed over
    };

    /**
     * @brief Run one configuration and judge it against what the driver's own
     *        arithmetic says it should do.
     * @param accelAskUs Accelerometer period asked for, 0 to leave it off.
     * @param gyroAskUs  Gyroscope period asked for, 0 to leave it off.
     * @param requestMs  Latency asked of every stream that is on, 0 for none.
     * @param accelRunUs Period the accelerometer should run at, when that is
     *                   not the one asked for; 0 for "as asked".
     * @param gyroRunUs  The same for the gyroscope.
     */
    void runConfig(uint32_t accelAskUs, uint32_t gyroAskUs, uint32_t requestMs,
                   uint32_t accelRunUs = 0, uint32_t gyroRunUs = 0)
    {
        mImu.setFifoLatency(Interface::IImu::FifoChannel::ACCEL, requestMs);
        mImu.setFifoLatency(Interface::IImu::FifoChannel::GYRO, requestMs);

        if (accelAskUs != 0) {
            mImu.enableAccel(accelAskUs);
        } else {
            mImu.disableAccel();
        }

        if (gyroAskUs != 0) {
            mImu.enableGyro(gyroAskUs);
        } else {
            mImu.disableGyro();
        }

        // What the part should run at is given here, not taken from the
        // driver's own choice of a rate: that choice is what is under test.
        const uint32_t accelUs = (accelRunUs != 0) ? accelRunUs : accelAskUs;
        const uint32_t gyroUs  = (gyroRunUs != 0) ? gyroRunUs : gyroAskUs;

        const uint32_t period = ImuFifoCadence::periodMs(accelUs != 0, requestMs, gyroUs != 0, requestMs);
        const uint16_t limit  = Hardware::Bmi270Fifo::limitWithReserve(
                accelUs, gyroUs, ImuFifoCadence::kReadReserveMs, true, Hardware::BMI270::kFifoWatermarkLimit);
        const uint32_t bytes  = Hardware::Bmi270Fifo::watermarkBytes(accelUs, gyroUs, period, true, limit);

        // How long the FIFO takes to fill to the watermark. One stream: a
        // frame of 7 bytes a period. Two: a shared frame of 13 bytes every
        // period of the slower one and a frame of 7 for the rest of the
        // faster one's readings, which comes to 6 / slow + 7 / fast bytes a
        // microsecond.
        uint64_t gapUs = 0;
        if (accelUs != 0 && gyroUs != 0) {
            const uint64_t slow = (accelUs > gyroUs) ? accelUs : gyroUs;
            const uint64_t fast = (accelUs > gyroUs) ? gyroUs : accelUs;
            gapUs = (static_cast<uint64_t>(bytes) * slow * fast) / (6 * fast + 7 * slow);
        } else {
            gapUs = (static_cast<uint64_t>(bytes) * (accelUs + gyroUs)) / 7;
        }

        // A read is taken to the end of the FIFO: the frames that come while
        // it runs go out with it, and the level starts from nothing when it
        // ends. So the reads are apart by the fill time and the read time
        // together, and a read carries the frames of both.
        const uint64_t readUs = static_cast<uint64_t>(bytes) * kFifoByteUs;
        if (readUs < gapUs) {
            gapUs = (gapUs * gapUs) / (gapUs - readUs);
        }

        const uint32_t gapMs = static_cast<uint32_t>(gapUs / 1000);

        char name[48];
        snprintf(name, sizeof(name), "A%lu G%lu req %lu",
                 static_cast<unsigned long>(accelAskUs), static_cast<unsigned long>(gyroAskUs),
                 static_cast<unsigned long>(requestMs));

        LOG_INFO("[%s] expect watermark %lu bytes, a read every %lu ms\n", name,
                 static_cast<unsigned long>(bytes), static_cast<unsigned long>(gapMs));

        // The step from the last frame at the old rate to the first at the
        // new one is neither period: let the change through, two reads at the
        // least, then measure from a clean start.
        OS::Delay((gapMs * 2 > 1500) ? gapMs * 2 : 1500);
        mAccelStats.restartAll(1, gapMs);
        mGyroStats.restartAll(1, gapMs);
        mSyncStats.restartAll(1, gapMs);

        const uint32_t measureMs = (gapMs * 6 + 500 > 3000) ? gapMs * 6 + 500 : 3000;
        OS::Delay(measureMs);

        if (accelUs != 0) {
            check(name, mAccelStats, expectFor(accelUs, gapMs, bytes, gyroUs), accelUs);
        }
        if (gyroUs != 0) {
            check(name, mGyroStats, expectFor(gyroUs, gapMs, bytes, accelUs), gyroUs);
        }
        if (accelUs != 0 && accelUs == gyroUs) {
            check(name, mSyncStats, expectFor(accelUs, gapMs, bytes, gyroUs), accelUs);
        }
    }

    /// How old the newest reading of a read may be when it is handed over:
    /// the time the read itself takes, which grows with its size, how far
    /// under a hand-over the time base starts a stream, and room for the
    /// millisecond clock and the reading thread. Stamps that fell behind show
    /// as tens and hundreds of milliseconds over this.
    static uint32_t ageLimitMs(uint32_t bytes)
    {
        return (ImuTimeBase::skFirstLeadUs / 1000) + 10 + bytes / 70;
    }

    /// The cadence a stream should show: the gap within 12 %, the frames a
    /// read carries within 15 % and one frame. A read is raised by the bytes of
    /// both streams, so the newest reading of the slower one may be up to its
    /// own period old by then.
    static Expect expectFor(uint32_t periodUs, uint32_t gapMs, uint32_t bytes, uint32_t otherPeriodUs)
    {
        const uint32_t frames = (gapMs * 1000) / periodUs;

        Expect e {};
        e.gapMinMs  = (gapMs * 88) / 100;
        e.gapMaxMs  = (gapMs * 112) / 100 + 2;
        e.framesMin = (frames * 85) / 100;
        e.framesMax = (frames * 115) / 100 + 1;

        if (e.framesMin == 0) {
            e.framesMin = 1;
        }

        e.ageMaxMs = ageLimitMs(bytes);
        if (otherPeriodUs != 0 && otherPeriodUs < periodUs) {
            e.ageMaxMs += periodUs / 1000;
        }

        return e;
    }

    /// Whether a step between two frames is the data period, to within the
    /// 2 % the part's oscillator may be off the system clock by.
    static bool nearPeriod(uint32_t stepUs, uint32_t periodUs)
    {
        return (static_cast<uint64_t>(stepUs) * 100 >= static_cast<uint64_t>(periodUs) * 98)
            && (static_cast<uint64_t>(stepUs) * 100 <= static_cast<uint64_t>(periodUs) * 102);
    }

    /**
     * @brief Print what a stream's timestamps did over a stretch at one rate
     *        and judge how even they were.
     *
     * None may go back or be ahead of the clock, and all the steps, inside
     * the reads and from read to read, must be within 0.3 % of one another.
     * Even steps of the wrong length would pass that, so the times must keep
     * pace with the clock too: the newest reading of a read is about as old
     * at the end of the stretch as at its start.
     */
    void checkEven(const char* name, StreamStats& stats, uint32_t periodUs)
    {
        const StreamStats::Snapshot s = stats.get();

        const uint32_t lo     = (s.joins != 0 && s.minJoinUs < s.minStepUs) ? s.minJoinUs : s.minStepUs;
        const uint32_t hi     = (s.joins != 0 && s.maxJoinUs > s.maxStepUs) ? s.maxJoinUs : s.maxStepUs;
        const uint32_t spread = hi - lo;

        const bool ok = (s.reads != 0) && (s.maxStepUs != 0) && (spread * 1000 <= periodUs * 3)
                     && (s.backwards == 0) && (s.future == 0) && ((s.maxAgeMs - s.minAgeMs) <= kPaceMs);

        LOG_INFO("[%s] reads %lu, step %lu..%lu us, join %lu..%lu us over %lu, spread %lu us, back %lu, "
                 "future %lu, age %ld..%ld ms -> %s\n",
                 name,
                 static_cast<unsigned long>(s.reads),
                 static_cast<unsigned long>(s.minStepUs),
                 static_cast<unsigned long>(s.maxStepUs),
                 static_cast<unsigned long>(s.minJoinUs),
                 static_cast<unsigned long>(s.maxJoinUs),
                 static_cast<unsigned long>(s.joins),
                 static_cast<unsigned long>(spread),
                 static_cast<unsigned long>(s.backwards),
                 static_cast<unsigned long>(s.future),
                 static_cast<long>(s.minAgeMs),
                 static_cast<long>(s.maxAgeMs),
                 ok ? "PASS" : "FAIL");

        if (!ok) {
            ++mFailures;
        }
    }

    /// After a long run: were all the frames there, and how steady was it.
    void soak(const char* name, StreamStats& stats, uint32_t rateHz)
    {
        const StreamStats::Snapshot s = stats.get();

        const uint32_t elapsed  = s.lastCountMs - s.firstCountMs;
        const uint32_t expected = (elapsed * rateHz) / 1000;
        const uint32_t diff     = (s.laterFrames > expected) ? s.laterFrames - expected
                                                             : expected - s.laterFrames;
        const bool ok = (s.reads > 0) && (diff * 50 <= expected) && (s.backwards == 0) && (s.future == 0);

        LOG_INFO("[soak %s] frames %lu of %lu, reads %lu, gap %lu..%lu ms, step %lu..%lu us, "
                 "back %lu, future %lu, age %ld..%ld ms -> %s\n",
                 name,
                 static_cast<unsigned long>(s.laterFrames),
                 static_cast<unsigned long>(expected),
                 static_cast<unsigned long>(s.reads),
                 static_cast<unsigned long>(s.minGapMs),
                 static_cast<unsigned long>(s.maxGapMs),
                 static_cast<unsigned long>(s.minStepUs),
                 static_cast<unsigned long>(s.maxStepUs),
                 static_cast<unsigned long>(s.backwards),
                 static_cast<unsigned long>(s.future),
                 static_cast<long>(s.minAgeMs),
                 static_cast<long>(s.maxAgeMs),
                 ok ? "PASS" : "FAIL");

        if (!ok) {
            ++mFailures;
        }
    }

    /**
     * @brief Print what a stream did and judge it.
     *
     * The cadence is judged by its averages: a single late read is reported
     * and not failed. The timestamps are judged read by read: none may go
     * back, none may be ahead of the clock, the step inside a read and from
     * one read into the next must be the data period to within what the
     * part's oscillator may be off by, and the newest reading of a read must
     * be no older than the read accounts for.
     */
    void check(const char* name, StreamStats& stats, const Expect& expect, uint32_t periodUs)
    {
        const StreamStats::Snapshot s = stats.get();

        const uint32_t avgGap    = (s.gaps != 0) ? s.sumGapMs / s.gaps : 0;
        const uint32_t avgFrames = (s.reads != 0) ? (s.frames * 10) / s.reads : 0;   // x10

        const bool cadenceOk = (s.gaps != 0)
                            && (avgGap >= expect.gapMinMs) && (avgGap <= expect.gapMaxMs)
                            && (avgFrames >= expect.framesMin * 10)
                            && (avgFrames <= expect.framesMax * 10);

        const bool joinOk = (s.joins == 0)
                         || (nearPeriod(s.minJoinUs, periodUs) && nearPeriod(s.maxJoinUs, periodUs));

        // Reads of one frame have no step inside them.
        const bool stepOk = (s.maxStepUs == 0)
                         || (nearPeriod(s.minStepUs, periodUs) && nearPeriod(s.maxStepUs, periodUs));

        const bool ageOk = (s.maxAgeMs <= static_cast<int32_t>(expect.ageMaxMs));

        const bool ok = cadenceOk && joinOk && stepOk && ageOk && (s.backwards == 0) && (s.future == 0);

        LOG_INFO("[%s] reads %lu, gap %lu..%lu avg %lu ms, late %lu, frames %lu..%lu avg %lu.%lu\n",
                 name,
                 static_cast<unsigned long>(s.reads),
                 static_cast<unsigned long>(s.minGapMs),
                 static_cast<unsigned long>(s.maxGapMs),
                 static_cast<unsigned long>(avgGap),
                 static_cast<unsigned long>(s.lateReads),
                 static_cast<unsigned long>(s.minFrames),
                 static_cast<unsigned long>(s.maxFrames),
                 static_cast<unsigned long>(avgFrames / 10),
                 static_cast<unsigned long>(avgFrames % 10));
        LOG_INFO("[%s] step %lu..%lu us, join %lu..%lu us over %lu, back %lu, future %lu, "
                 "age %ld..%ld of %lu ms -> %s\n",
                 name,
                 static_cast<unsigned long>(s.minStepUs),
                 static_cast<unsigned long>(s.maxStepUs),
                 static_cast<unsigned long>(s.minJoinUs),
                 static_cast<unsigned long>(s.maxJoinUs),
                 static_cast<unsigned long>(s.joins),
                 static_cast<unsigned long>(s.backwards),
                 static_cast<unsigned long>(s.future),
                 static_cast<long>(s.minAgeMs),
                 static_cast<long>(s.maxAgeMs),
                 static_cast<unsigned long>(expect.ageMaxMs),
                 ok ? "PASS" : "FAIL");

        if (!ok) {
            ++mFailures;
        }
    }

    Interface::IImu& mImu;

    /// While set, the stream callbacks count instead of printing.
    bool        mQuiet    = false;
    uint32_t    mFailures = 0;
    StreamStats mAccelStats;
    StreamStats mGyroStats;
    StreamStats mSyncStats;

    // SensorCallback overrides
    void onAccelRaw(const Interface::IImu::SensorRawData* data, size_t count, float scale, uint32_t periodUs) override
    {
        (void)periodUs;

        if (mQuiet) {
            if (count > 0) {
                mAccelStats.add(data, count);
            }
            return;
        }

        LOG_INFO("Accel: %d samples, scale %.6f\n", count, scale);
        if (count > 0) {
            LOG_INFO("  [%3d] t=%.1f x=%d y=%d z=%d\n", 0,
                    data[0].timestamp / 1000.0f, data[0].x, data[0].y, data[0].z);
            if (count > 1) {
                uint16_t last = count - 1;
                LOG_INFO("  [%3d] t=%.1f, Delta: %.1f ms\n", last, data[last].timestamp / 1000.0f,
                          (data[last].timestamp - data[0].timestamp) / 1000.0f);
                (void)last;
            }
        }

    }

    void onGyroRaw(const Interface::IImu::SensorRawData* data, size_t count, float scale, uint32_t periodUs) override
    {
        (void)periodUs;

        if (mQuiet) {
            if (count > 0) {
                mGyroStats.add(data, count);
            }
            return;
        }

        LOG_INFO("Gyro: %d samples, scale %.6f\n", count, scale);
        if (count > 0) {
            LOG_INFO("  [%3d] t=%.1f x=%d y=%d z=%d\n", 0,
                    data[0].timestamp / 1000.0f, data[0].x, data[0].y, data[0].z);
            if (count > 1) {
                uint16_t last = count - 1;
                LOG_INFO("  [%3d] t=%.1f, Delta: %.1f ms\n", last, data[last].timestamp / 1000.0f,
                          (data[last].timestamp - data[0].timestamp) / 1000.0f);
                (void)last;
            }
        }
    }

    void onSyncRaw(const Interface::IImu::SyncSampleData* data, size_t count, float accelScale, float gyroScale,
                   uint32_t periodUs) override
    {
        (void)periodUs;

        if (mQuiet) {
            if (count > 0) {
                mSyncStats.add(data, count);
            }
            return;
        }

        LOG_INFO("Sync: %d samples, accel scale %.6f, gyro  scale %.6f\n", count, accelScale, gyroScale);
        if (count > 0) {
            LOG_INFO("  [%3d] t=%.1f ax=%d ay=%d az=%d gx=%d gy=%d gz=%d\n", 0,
                    data[0].timestamp / 1000.0f,
                    data[0].accel_x, data[0].accel_y, data[0].accel_z,
                    data[0].gyro_x, data[0].gyro_y, data[0].gyro_z);
            if (count > 1) {
                uint16_t last = count - 1;
                LOG_INFO("  [%3d] t=%.1f, Delta: %.1f ms\n", last, data[last].timestamp / 1000.0f,
                          (data[last].timestamp - data[0].timestamp) / 1000.0f);
                (void)last;
            }
        }
    }

    void onStepCounter(uint32_t steps, uint64_t timestamp) override
    {
        LOG_INFO("Steps: %lu steps, t=%.1f\n", steps, timestamp / 1000.0f);
    }

    void onStepDetector(uint64_t timestamp) override
    {
        LOG_INFO("Step detected, t=%.1f\n", timestamp / 1000.0f);
    }

    void onActivity(Interface::IImu::ActivityType type, uint64_t timestamp) override
    {
        const char* activityStr = "Unknown";
        switch (type) {
            case Interface::IImu::ActivityType::STILL:   activityStr = "Still"; break;
            case Interface::IImu::ActivityType::WALKING: activityStr = "Walking"; break;
            case Interface::IImu::ActivityType::RUNNING: activityStr = "Running"; break;
            default: activityStr = "Unknown"; break;
        }
        LOG_INFO("Activity: %s (%d), t=%.1f\n", activityStr, timestamp / 1000.0f);
    }

    void onWristWakeup(uint64_t timestamp) override
    {
        LOG_INFO("Wrist wakeup, t=%.1f\n", timestamp / 1000.0f);
    }

    void onNoMotion(uint64_t timestamp) override
    {
        LOG_INFO("No motion, t=%.1f\n", timestamp / 1000.0f);
    }

    void onMotion(uint64_t timestamp) override
    {
        LOG_INFO("Any motion, t=%.1f\n", timestamp / 1000.0f);
    }

    void onSigMotion(uint64_t timestamp) override
    {
        LOG_INFO("Significant motion, t=%.1f\n", timestamp / 1000.0f);
    }
};

void TestIMU_execute()
{
    LOG_INFO("start\n");

#if 1

    Interface::IImu& imu = ComponentFactory::GetInstance().getImu();

    TestIMU2 *test = new TestIMU2(imu);
    assert(test);

    while (1) {
            imu.init();

            LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
            LOG_INFO_WP("//// TESTING IMU\n");
            LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

            //test->run_accel();
            //test->run_accel_dyn_odr();
            //test->run_accel_gyro_dyn_odr();
            //test->run_step_counter();
            //test->run_activity_recognition();
            //test->run_motion_interrupts();
            //test->run_wrist_wakeup();
            //test->run_temperature(); // Valid only if gyroscope is enabled
            //test->run_accel_features();
            //test->run_sync();
            //test->run_restart_after_pause(); // Six minutes
            test->run_fifo_latency();
            test->run_gyro_switch();
            test->run_fifo_matrix();


            imu.deinit();

            OS::Delay(3000);
        }
#endif
}
