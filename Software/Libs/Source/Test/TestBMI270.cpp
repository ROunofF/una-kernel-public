/**
 ******************************************************************************
 * @file    TestBMI270.cpp
 * @brief   On-device test of the BMI270 driver on its own: the counter value
 *          every FIFO frame is given, across the rates and the changes of rate
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestBMI270"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestBMI270.hpp"
#include "Hardware/BMI270/BMI270.hpp"
#include "Hardware/BMI270/Bmi270Odr.hpp"
#include "Hardware/BMI270/FifoGrid.hpp"
#include "Gpio.hpp"
#include "System.hpp"
#include "Instances/InstanceI2C.hpp"
#include "OS.hpp"

#include <cassert>

namespace
{

using Hardware::BMI270;
using Hardware::Bmi270Fifo::isAfter;
using Hardware::Bmi270Fifo::kCounterMask;

/// Data period of a rate in whole Hz.
constexpr uint32_t hz(uint32_t rate)
{
    return 1000000u / rate;
}

/**
 * @brief What the driver promises of one stream's frames, checked batch by
 *        batch as they are given out.
 *
 * Every frame carries the value the part's counter had when it was taken.
 * That value is a whole multiple of the period the batch was taken at; in a
 * batch the frames are one period apart; and from one frame to the next it
 * only ever increases, across batches too. Two batches at one rate join a
 * period apart unless the part lost frames, which this test gives it no
 * cause to.
 */
class StreamCheck
{
public:
    /// Forget the stream: the part was started anew, and its counter with it.
    void forget()
    {
        restart();
        mHaveLast = false;
    }

    /// Start counting anew; the stream runs on.
    void restart()
    {
        mBatches       = 0;
        mFrames        = 0;
        mOffGrid       = 0;
        mBadStep       = 0;
        mNotIncreasing = 0;
        mGaps          = 0;
        mStaleTime     = 0;
        mTimeBack      = 0;
    }

    /// One batch: the frames' counter values, the period they were taken at,
    /// the time the driver says their read was over and the time now.
    template <typename Frame>
    void add(const Frame* frames, uint16_t count, uint32_t periodUs, uint64_t readTimeUs, uint64_t nowUs)
    {
        if (count == 0) {
            return;
        }

        // The read was over just before its batches came: a time from long
        // ago is that of another read.
        if (readTimeUs > nowUs || (nowUs - readTimeUs) > kFreshUs) {
            ++mStaleTime;
        }

        if (readTimeUs < mLastReadUs) {
            ++mTimeBack;
        }

        mLastReadUs = readTimeUs;

        // One count is 39.0625 us.
        const uint32_t step = (periodUs * 16u) / 625u;

        if (mBatches == 0) {
            mFirstPeriodUs = periodUs;
        }

        ++mBatches;
        mFrames  += count;
        mPeriodUs = periodUs;

        for (uint16_t i = 0; i < count; ++i) {
            const uint32_t at = frames[i].sensorTime;

            if (step == 0 || at > kCounterMask || (at % step) != 0) {
                ++mOffGrid;
            }

            if (i != 0 && ((at - frames[i - 1].sensorTime) & kCounterMask) != step) {
                ++mBadStep;
            }
        }

        if (mHaveLast) {
            if (!isAfter(frames[0].sensorTime, mLast)) {
                ++mNotIncreasing;
            } else if (step == mLastStep && ((frames[0].sensorTime - mLast) & kCounterMask) != step) {
                ++mGaps;
                LOG_INFO("    gap: %lu after %lu, step %lu\n", static_cast<unsigned long>(frames[0].sensorTime),
                         static_cast<unsigned long>(mLast), static_cast<unsigned long>(step));
            }
        }

        mLast     = frames[count - 1].sensorTime;
        mLastStep = step;
        mHaveLast = true;
    }

    /// @param framesLost Whether the part was made to lose frames: then
    ///                   batches must have joined more than a period apart.
    bool ok(bool framesLost) const
    {
        return (mOffGrid == 0) && (mBadStep == 0) && (mNotIncreasing == 0) && ((mGaps != 0) == framesLost)
            && (mStaleTime == 0) && (mTimeBack == 0);
    }

    uint32_t frames() const
    {
        return mFrames;
    }

    uint32_t periodUs() const
    {
        return mPeriodUs;
    }

    /// The period of the first batch since the count was started anew.
    uint32_t firstPeriodUs() const
    {
        return mFirstPeriodUs;
    }

    void print(const char* step, const char* stream, bool pass) const
    {
        LOG_INFO("[%s] %s: %lu frames in %lu batches, now %lu us, off grid %lu, bad step %lu, "
                 "not later %lu, gaps %lu, read time stale %lu, back %lu -> %s\n", step, stream,
                 static_cast<unsigned long>(mFrames), static_cast<unsigned long>(mBatches),
                 static_cast<unsigned long>(mPeriodUs), static_cast<unsigned long>(mOffGrid),
                 static_cast<unsigned long>(mBadStep), static_cast<unsigned long>(mNotIncreasing),
                 static_cast<unsigned long>(mGaps), static_cast<unsigned long>(mStaleTime),
                 static_cast<unsigned long>(mTimeBack), pass ? "PASS" : "FAIL");
    }

private:
    /// How long after its read a batch may come, us.
    static constexpr uint64_t kFreshUs = 10000;

    uint32_t mBatches       = 0;
    uint32_t mFrames        = 0;
    uint32_t mPeriodUs      = 0;        ///< Of the batch given out last
    uint32_t mFirstPeriodUs = 0;        ///< Of the first batch counted
    uint32_t mOffGrid       = 0;        ///< Values that are no multiple of the period
    uint32_t mBadStep       = 0;        ///< Frames of a batch not one period apart
    uint32_t mNotIncreasing = 0;        ///< Batches that did not start after the one before
    uint32_t mGaps          = 0;        ///< Batches at one rate that did not join a period apart
    uint32_t mStaleTime     = 0;        ///< Batches whose read time was not from just before
    uint32_t mTimeBack      = 0;        ///< Batches whose read time was earlier than the last
    uint64_t mLastReadUs    = 0;
    uint32_t mLast          = 0;
    uint32_t mLastStep      = 0;
    bool     mHaveLast      = false;
};

/**
 * @brief The BMI270 driver on its own: nothing above it but this test.
 *
 * The test owns the part: it asks for the sensors, sets the FIFO up, watches
 * the interrupt pin and has the driver serve it, and checks what the driver
 * gives its listener against what the driver promises.
 */
class TestBmi270 : public BMI270::IListener
{
public:
    TestBmi270()
        : mDrv(Instance::I2C::getInstance().getSensor2Bus())
        , mPin(Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_INT2))
    {
    }

    void run()
    {
        LOG_INFO("=== BMI270 driver ===\n");

        mFailures = 0;
        mFifo     = {};
        mAccel.forget();
        mGyro.forget();
        mSync.forget();
        mPin.init(Interface::IGpi::Pull::DOWN);

        BMI270::InterruptConfig pin {};
        pin.outputEnabled = true;
        pin.activeHigh    = true;
        pin.pushPull      = true;
        pin.latched       = true;
        mDrv.setInterruptConfig(BMI270::InterruptPin::INT2, pin);
        mDrv.setListener(this);

        // One stream, up and down the rates: a slower rate takes effect only
        // when the two rates' readings fall on one instant, up to two of its
        // periods later, and the part goes on at the old rate until then.
        step("XL 1600", hz(1600), 0, 600);
        step("XL 1600 -> 0.78", 1280000, 0, 4500);
        step("XL 0.78 -> 1600", hz(1600), 0, 500);
        step("XL 1600 -> 100", hz(100), 0, 400);
        step("XL 100 -> 25", hz(25), 0, 500);
        step("XL 25 -> 200", hz(200), 0, 400);

        // Settings the part cannot run - this rate with the filter in its
        // power-optimised mode - are refused unwritten, and the stream runs on
        // as it was, with no frame lost.
        refused("XL 200, 1600 refused", hz(1600), hz(200), 400);

        // The second stream joins and changes under the first.
        step("+ GY 100", hz(200), hz(100), 400);
        step("GY 100 -> 1600", hz(200), hz(1600), 400);
        step("GY 1600 -> 25", hz(200), hz(25), 500);

        // Both at one rate go out as pairs too; then both change at once, and
        // one changes away from the other.
        step("both 50", hz(50), hz(50), 500);
        step("both 50 -> 400", hz(400), hz(400), 400);
        step("XL 400 -> 25, GY 400", hz(25), hz(400), 500);
        step("both 1600", hz(1600), hz(1600), 1000);
        step("- GY", hz(1600), 0, 500);
        step("XL 1600 -> 12.5", 80000, 0, 600);

        // The part's word for a new rate is lost with the FIFO's contents:
        // the frames themselves have to show the rate they were taken at.
        flushed("XL 12.5 -> 400, FIFO flushed", hz(400), 500);

        // A FIFO left to overflow drops its oldest frames. What it holds is
        // read all the same, and the loss shows as a wider step between two
        // batches, nowhere else.
        step("both 1600 again", hz(1600), hz(1600), 500);
        servedLate("both 1600, served late", hz(1600), 300, 700);

        // A rate written twice before the first write has taken effect: the
        // part takes the last one, with one word for the change.
        step("- GY again", hz(1600), 0, 300);
        twice("XL 1600, then 25 and 100", hz(25), hz(100), 500);
        twice("XL 100, then 25 and 400", hz(25), hz(400), 400);

        // A stream that runs outside the FIFO and comes into it as a slower
        // rate is written. The part goes on at the old rate for up to two
        // periods of the new one, and those frames have to come out too, at
        // the rate they were taken at. The slowest rate there is, so that
        // the old one is still running when the FIFO is set up.
        joined("XL joins at 0.78 from 1600", hz(1600), 1280000, 4500);

        mDrv.releaseAll();
        mDrv.setListener(nullptr);
        mPin.deinit();

        LOG_INFO("BMI270 driver test completed: %lu failure(s)\n", static_cast<unsigned long>(mFailures));
    }

    void onFifoAccel(const BMI270::SensorRawData* data, uint16_t count, float scale, uint64_t readTimeUs) override
    {
        (void)scale;
        mAccel.add(data, count, mDrv.getAppliedPeriodUs(BMI270::SensorType::ACCELEROMETER), readTimeUs,
                   mDrv.getSystemTime());
    }

    void onFifoGyro(const BMI270::SensorRawData* data, uint16_t count, float scale, uint64_t readTimeUs) override
    {
        (void)scale;
        mGyro.add(data, count, mDrv.getAppliedPeriodUs(BMI270::SensorType::GYROSCOPE), readTimeUs,
                  mDrv.getSystemTime());
    }

    void onFifoSync(const BMI270::SyncSampleData* data, uint16_t count, float accelScale, float gyroScale,
                    uint64_t readTimeUs) override
    {
        (void)accelScale;
        (void)gyroScale;
        mSync.add(data, count, mDrv.getAppliedPeriodUs(BMI270::SensorType::ACCELEROMETER), readTimeUs,
                  mDrv.getSystemTime());
    }

private:
    /**
     * @brief Set the streams to these periods, 0 for off, serve the part for
     *        a while and judge what came.
     *
     * The frames are judged from the moment the rates are written, so those
     * the part still takes at the old rate are in.
     */
    void step(const char* name, uint32_t accelUs, uint32_t gyroUs, uint32_t forMs)
    {
        LOG_INFO("--- %s\n", name);

        const bool fifoChanges = ((accelUs != 0) != mFifo.accelEnabled) || ((gyroUs != 0) != mFifo.gyroEnabled);

        mAccel.restart();
        mGyro.restart();
        mSync.restart();

        // A stream that was off starts anew: nothing ties its first frame to
        // the last one it gave before.
        if (!mFifo.accelEnabled) {
            mAccel.forget();
        }
        if (!mFifo.gyroEnabled) {
            mGyro.forget();
            mSync.forget();
        }

        apply(BMI270::SensorType::ACCELEROMETER, accelUs, 8);
        apply(BMI270::SensorType::GYROSCOPE, gyroUs, 2000);

        mFifo.accelEnabled      = (accelUs != 0);
        mFifo.gyroEnabled       = (gyroUs != 0);
        mFifo.headerEnabled     = true;
        mFifo.sensorTimeEnabled = true;
        mFifo.interruptPin      = BMI270::InterruptPin::INT2;
        mFifo.watermarkBytes    = mDrv.calculateFifoWatermark(kReadMs, true, kReserveMs);

        if (fifoChanges) {
            mDrv.configureFifo(mFifo);
        } else {
            mDrv.setFifoWatermark(mFifo.watermarkBytes);
        }

        serve(forMs);

        judge(name, "accel", mAccel, accelUs);
        judge(name, "gyro", mGyro, gyroUs);

        if (accelUs != 0 && accelUs == gyroUs) {
            judge(name, "pairs", mSync, accelUs);
        }
    }

    /// Write the accelerometer's rate twice on end and judge the stream at
    /// the second.
    void twice(const char* name, uint32_t firstUs, uint32_t secondUs, uint32_t forMs)
    {
        LOG_INFO("--- %s\n", name);

        mAccel.restart();

        apply(BMI270::SensorType::ACCELEROMETER, firstUs, 8);
        apply(BMI270::SensorType::ACCELEROMETER, secondUs, 8);
        mDrv.setFifoWatermark(mDrv.calculateFifoWatermark(kReadMs, true, kReserveMs));

        serve(forMs);
        judge(name, "accel", mAccel, secondUs);
    }

    /// Take the accelerometer out of the FIFO, run it there at one rate, then
    /// ask for a slower one and bring it in. The part goes on at the old rate
    /// until the new one takes effect, so the first batch has to be at the
    /// old rate, whatever was written.
    void joined(const char* name, uint32_t runsUs, uint32_t accelUs, uint32_t forMs)
    {
        mDrv.disableFifo();
        apply(BMI270::SensorType::ACCELEROMETER, 0, 8);
        apply(BMI270::SensorType::GYROSCOPE, 0, 2000);
        mFifo.accelEnabled = false;
        mFifo.gyroEnabled  = false;

        apply(BMI270::SensorType::ACCELEROMETER, runsUs, 8);
        OS::Delay(300);

        step(name, accelUs, 0, forMs);

        const bool pass = (mAccel.firstPeriodUs() == runsUs);

        LOG_INFO("[%s] accel: first batch at %lu us, the sensor ran at %lu us -> %s\n", name,
                 static_cast<unsigned long>(mAccel.firstPeriodUs()), static_cast<unsigned long>(runsUs),
                 pass ? "PASS" : "FAIL");

        if (!pass) {
            ++mFailures;
        }
    }

    /// Change the accelerometer's rate and flush the FIFO once the part has
    /// put its word for the change there.
    void flushed(const char* name, uint32_t accelUs, uint32_t forMs)
    {
        LOG_INFO("--- %s\n", name);

        mAccel.restart();

        apply(BMI270::SensorType::ACCELEROMETER, accelUs, 8);
        OS::Delay(10);
        mDrv.flushFifo();
        mDrv.setFifoWatermark(mDrv.calculateFifoWatermark(kReadMs, true, kReserveMs));

        serve(forMs);
        judge(name, "accel", mAccel, accelUs);
    }

    /// Leave the part unserved long enough for its FIFO to overflow, then
    /// serve it. Both streams run at one period.
    void servedLate(const char* name, uint32_t periodUs, uint32_t lateMs, uint32_t forMs)
    {
        LOG_INFO("--- %s\n", name);

        mAccel.restart();
        mGyro.restart();
        mSync.restart();

        OS::Delay(lateMs);
        serve(forMs);

        judge(name, "accel", mAccel, periodUs, true);
        judge(name, "gyro", mGyro, periodUs, true);
        judge(name, "pairs", mSync, periodUs, true);
    }

    /// Ask the accelerometer for a period with settings the part refuses, and
    /// judge that the stream goes on at the period it had.
    void refused(const char* name, uint32_t askUs, uint32_t runsUs, uint32_t forMs)
    {
        LOG_INFO("--- %s\n", name);

        mAccel.restart();

        BMI270::SensorConfig config {};
        config.periodUs        = askUs;
        config.range           = 8;
        config.performanceMode = false;

        if (mDrv.requestSensor(BMI270::SensorType::ACCELEROMETER, config)) {
            LOG_INFO("[%s] the part took the settings -> FAIL\n", name);
            ++mFailures;
        }

        serve(forMs);
        judge(name, "accel", mAccel, runsUs);
    }

    /// Have the driver serve the part's interrupt for a while.
    void serve(uint32_t forMs)
    {
        const uint32_t start = Driver::System::GetTimeMs();

        while (Driver::System::GetTimeMs() - start < forMs) {
            if (mPin.get()) {
                mDrv.processInterrupt();
            } else {
                OS::Delay(1);
            }
        }
    }

    /// A stream that is on has given frames, they keep the driver's promises,
    /// and by the end of the step they come at the rate asked for. Batches at
    /// one rate join a period apart, unless the part was made to lose frames.
    void judge(const char* step, const char* stream, const StreamCheck& check, uint32_t periodUs,
               bool framesLost = false)
    {
        if (periodUs == 0) {
            return;
        }

        const bool pass = check.ok(framesLost) && (check.frames() != 0) && (check.periodUs() == periodUs);

        check.print(step, stream, pass);

        if (!pass) {
            ++mFailures;
        }
    }

    void apply(BMI270::SensorType sensor, uint32_t periodUs, uint16_t range)
    {
        if (periodUs == 0) {
            if (mDrv.isSensorEnabled(sensor)) {
                mDrv.releaseSensor(sensor);
            }
            return;
        }

        BMI270::SensorConfig config {};
        config.periodUs = periodUs;
        config.range    = range;

        // The fast rates need the filter in its performance mode: the part
        // refuses them in the power-optimised one.
        config.performanceMode = (sensor == BMI270::SensorType::ACCELEROMETER) ? (periodUs < 5000)
                                                                               : (periodUs < 10000);

        mDrv.requestSensor(sensor, config);
    }

    static constexpr uint32_t kReadMs    = 100;
    static constexpr uint32_t kReserveMs = 40;

    BMI270             mDrv;
    Driver::Gpio&      mPin;
    BMI270::FifoConfig mFifo {};
    StreamCheck        mAccel;
    StreamCheck        mGyro;
    StreamCheck        mSync;
    uint32_t           mFailures = 0;
};

} // namespace

void TestBMI270_execute()
{
    TestBmi270* test = new TestBmi270();
    assert(test);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING BMI270\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test->run();

        OS::Delay(3000);
    }
}
