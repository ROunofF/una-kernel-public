/**
 ******************************************************************************
 * @file    TestSensorRate.cpp
 * @brief   On-device test of the rate a SensorLayer listener is given,
 *          source by source
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestSensorRate"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestSensorRate.hpp"
#include "Test/SensorProbe.hpp"
#include "System.hpp"
#include "OS.hpp"
#include "Sleep.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

#include <cstdint>

/**
 * @brief What rate a listener is given, source by source.
 *
 * A listener asks for a period; each queue passes every N-th sample of its
 * source, N = floor(period / source step), with the step the source reports
 * as set, not as measured. Every scenario checks the samples a second a
 * listener gets and how far apart they are stamped - a sample passed or
 * dropped out of turn shows as a step of one source step or of three - and
 * the cadence and timestamps as the latency test does. Keep the watch still.
 */
class TestSensorRate : public SensorProbeTest
{
public:
    TestSensorRate()
        : mSlow("XL 40/100", SDK::Sensor::Type::ACCELEROMETER)
        , mRaw("XLraw 20/1000", SDK::Sensor::Type::ACCELEROMETER_RAW)
        , mSteps("Steps", SDK::Sensor::Type::STEP_COUNTER)
        , mGyro("Gyro 20/200", SDK::Sensor::Type::GYROSCOPE)
        , mGyroSlow("Gyro 40/200", SDK::Sensor::Type::GYROSCOPE)
        , mFusionRaw("FusionRaw 10/100", SDK::Sensor::Type::FUSION_RAW)
        , mFusionRawSlow("FusionRaw 20/100", SDK::Sensor::Type::FUSION_RAW)
        , mFusion("Fusion 20/100", SDK::Sensor::Type::FUSION)
        , mMagRaw("MagRaw 10/200", SDK::Sensor::Type::MAGNETIC_FIELD_RAW)
        , mMagRawSlow("MagRaw 40/200", SDK::Sensor::Type::MAGNETIC_FIELD_RAW)
        , mMag("Mag 40/200", SDK::Sensor::Type::MAGNETIC_FIELD)
        , mMagOdd("MagRaw 40/110", SDK::Sensor::Type::MAGNETIC_FIELD_RAW)
        , mPress("Press 200/200", SDK::Sensor::Type::PRESSURE)
        , mPressFast("Press 100/1000", SDK::Sensor::Type::PRESSURE)
        , mPressSlow("Press 1000/1000", SDK::Sensor::Type::PRESSURE)
        , mPressOdd("Press 200/500", SDK::Sensor::Type::PRESSURE)
        , mAlt("Alt 1000/1000", SDK::Sensor::Type::ALTIMETER)
        , mAltFast("Alt 200/200", SDK::Sensor::Type::ALTIMETER)
        , mTemp("Temp 1000/1000", SDK::Sensor::Type::AMBIENT_TEMPERATURE)
    {
    }

    void run()
    {
        LOG_INFO("=== Sensor rate test ===\n");

        mFailures = 0;

        r1();
        r2();
        r3();
        r4();
        r5();
        r6();
        r7();
        r8();
        r9();

        LOG_INFO("Sensor rate test completed: %lu failure(s)\n", static_cast<unsigned long>(mFailures));
    }

private:
    // The accelerometer at 25 Hz, read every 80 ms: two frames a delivery.
    static constexpr Expect kTwoFrames  { 70, 95, 15, 30, 140 };
    // A 40 ms listener beside a 50 Hz one: every second of the five frames a
    // 100 ms read brings, two or three of them, in one delivery a read.
    static constexpr Expect kBeside50Hz { 88, 112, 20, 30, 140 };
    // A second asked for at 50 Hz: a batch a second.
    static constexpr Expect kSecond50Hz { 850, 1100, 250, 600, 1100 };

    /// A 40 ms listener: 25 a second, each 40 ms after the last as the
    /// part's own clock runs, a few per cent either way.
    static constexpr Rate kEvery40Ms { 240, 260, 38000, 42000 };

    // The processed gyroscope at 50 Hz, read every 200 ms: ten frames a
    // delivery, or five to a 40 ms listener.
    static constexpr Expect kGyro     { 176, 224, 80, 120, 260 };
    static constexpr Expect kGyroHalf { 176, 224, 40, 60, 260 };

    // Both IMU streams at 100 Hz, read every 100 ms: ten pairs a delivery,
    // or five to a 20 ms listener.
    static constexpr Expect kTenFrames  { 88, 112, 80, 120, 140 };
    static constexpr Expect kFiveFrames { 88, 112, 40, 60, 140 };

    /// A 20 ms listener: 50 a second, 20 ms apart.
    static constexpr Rate kEvery20Ms { 490, 510, 19000, 21000 };

    // The magnetometer at 100 Hz, handed over every 200 ms: twenty
    // measurements a delivery, or five to a 40 ms listener. The part has no
    // FIFO, so the cadence is the sweep's and is judged loosely.
    static constexpr Expect kMagTwenty { 150, 250, 150, 250, 260 };
    static constexpr Expect kMagFive   { 150, 250, 30, 70, 260 };

    // A barometer sample carries the instant its measurement was started
    // and is handed on when the measurement, about 50 ms, is done: that is
    // its age. One listener's ring is one sample.
    static constexpr Expect kPressEach { 180, 220, 10, 10, 80 };
    static constexpr Expect kPressSec  { 950, 1050, 10, 10, 80 };
    static constexpr Expect kPressTen  { 900, 1100, 90, 110, 1100 };

    // The part measures on a grid of its own, so a step is its period to a
    // couple of milliseconds: one or two for a wake-up out of deep sleep.
    static constexpr Rate kEvery200Ms { 48, 52, 195000, 205000 };
    static constexpr Rate kEvery1s    { 9, 11, 995000, 1005000 };
    static constexpr Rate kEvery100Ms { 97, 103, 95000, 105000 };
    /// The same with no deep sleep: whether the spread comes from waking.
    static constexpr Rate kEvery100MsAwake { 97, 103, 98000, 102000 };

    // A latency of over two periods and under three on a part that gives
    // one measurement at a time: the three that lie within it in a
    // delivery. The barometer's fill the queue as they come, the oldest two
    // periods old and the 50 ms a measurement takes. The magnetometer's
    // are taken up on the sweep, and the sweep that takes up the third is
    // the one that comes round as the first falls due: the oldest is as old
    // as the latency, a millisecond or two either way. Its latency is
    // 110 ms and not 100 so that the third, ready 100 ms after the first
    // was taken, is there by then.
    static constexpr Expect kMagOdd   { 105, 135, 30, 30, 115 };
    static constexpr Expect kPressOdd { 570, 630, 30, 30, 500 };

    /**
     * R1. A 40 ms listener of a 50 Hz stream gets every second sample, 25 a
     *     second and evenly spaced, whichever way this part's oscillator is
     *     off. The 20 ms raw listener beside it runs the accelerometer at
     *     50 Hz.
     */
    void r1()
    {
        mRaw.connect(20, 1000);
        mSlow.connect(40, 100);
        OS::Delay(1500);
        mSlow.restartAll(kBeside50Hz.gapMaxMs, kBeside50Hz.oldestAvgMaxMs);
        mRaw.restartAll(kSecond50Hz.gapMaxMs, kSecond50Hz.oldestAvgMaxMs);
        OS::Delay(6500);
        check("R1 40/100 beside 20/1000", mSlow, kBeside50Hz);
        check("R1 20/1000", mRaw, kSecond50Hz);
        checkRate("R1 40 ms from 50 Hz", mSlow, kEvery40Ms);
        mSlow.disconnect();
        mRaw.disconnect();
        OS::Delay(500);
    }

    /**
     * R2. The compass's case. A chip feature holds the accelerometer at 50 Hz
     *     while it is on, so a 40 ms listener faces a 20 ms stream whenever
     *     the step counter runs, and a 40 ms one when it stops. Both ways it
     *     is given 25 samples a second, evenly spaced, and the rate changing
     *     under it does not upset the stamps.
     */
    void r2()
    {
        mSlow.connect(40, 100);
        measure(mSlow, kTwoFrames, 5000, true);
        check("R2 40/100 at 25 Hz", mSlow, kTwoFrames);
        checkRate("R2 40 ms from 25 Hz", mSlow, kEvery40Ms);

        mSteps.connect(0, 0);
        measure(mSlow, kBeside50Hz, 8000, false);
        check("R2 40/100 under the feature floor", mSlow, kBeside50Hz);
        checkRate("R2 40 ms from 50 Hz", mSlow, kEvery40Ms);

        mSteps.disconnect();
        measure(mSlow, kTwoFrames, 5000, false);
        check("R2 40/100 back at 25 Hz", mSlow, kTwoFrames);
        checkRate("R2 40 ms from 25 Hz again", mSlow, kEvery40Ms);
        mSlow.disconnect();
        OS::Delay(500);
    }

    /**
     * R3. The processed gyroscope decimated: a 40 ms listener beside a 20 ms
     *     one gets every second sample of the 50 Hz stream, five to a
     *     delivery of 200 ms.
     */
    void r3()
    {
        mGyro.traceShortGaps(true);
        mGyroSlow.traceShortGaps(true);
        mGyro.connect(20, 200);
        mGyroSlow.connect(40, 200);
        OS::Delay(1500);
        mGyro.restartAll(kGyro.gapMaxMs, kGyro.oldestAvgMaxMs);
        mGyroSlow.restartAll(kGyroHalf.gapMaxMs, kGyroHalf.oldestAvgMaxMs);
        OS::Delay(8000);
        check("R3 gyro 20/200", mGyro, kGyro);
        check("R3 gyro 40/200", mGyroSlow, kGyroHalf);
        checkRate("R3 gyro 40 ms from 50 Hz", mGyroSlow, kEvery40Ms);
        mGyroSlow.disconnect();
        mGyro.disconnect();
        OS::Delay(500);
    }

    /**
     * R4. The paired streams decimated, raw and processed: beside a 10 ms
     *     listener that runs both parts at 100 Hz, a 20 ms one of either
     *     kind gets every second pair, five to a delivery.
     */
    void r4()
    {
        mFusionRaw.traceShortGaps(true);
        mFusionRawSlow.traceShortGaps(true);
        mFusion.traceShortGaps(true);
        mFusionRaw.connect(10, 100);
        mFusionRawSlow.connect(20, 100);
        mFusion.connect(20, 100);
        OS::Delay(1500);
        mFusionRaw.restartAll(kTenFrames.gapMaxMs, kTenFrames.oldestAvgMaxMs);
        mFusionRawSlow.restartAll(kFiveFrames.gapMaxMs, kFiveFrames.oldestAvgMaxMs);
        mFusion.restartAll(kFiveFrames.gapMaxMs, kFiveFrames.oldestAvgMaxMs);
        OS::Delay(8000);
        check("R4 fusion raw 10/100", mFusionRaw, kTenFrames);
        check("R4 fusion raw 20/100", mFusionRawSlow, kFiveFrames);
        check("R4 fusion 20/100", mFusion, kFiveFrames);
        checkRate("R4 fusion raw 20 ms from 100 Hz", mFusionRawSlow, kEvery20Ms);
        checkRate("R4 fusion 20 ms from 100 Hz", mFusion, kEvery20Ms);
        mFusion.disconnect();
        mFusionRawSlow.disconnect();
        mFusionRaw.disconnect();
        OS::Delay(500);
    }

    /**
     * R5. The magnetometer, raw and calibrated: beside a 10 ms listener that
     *     runs the part at 100 Hz, a 40 ms one of either kind gets every
     *     fourth measurement.
     */
    void r5()
    {
        mMagRawSlow.traceShortGaps(true);
        mMag.traceShortGaps(true);
        mMagRaw.connect(10, 200);
        mMagRawSlow.connect(40, 200);
        mMag.connect(40, 200);
        OS::Delay(1500);
        mMagRaw.restartAll(kMagTwenty.gapMaxMs, kMagTwenty.oldestAvgMaxMs);
        mMagRawSlow.restartAll(kMagFive.gapMaxMs, kMagFive.oldestAvgMaxMs);
        mMag.restartAll(kMagFive.gapMaxMs, kMagFive.oldestAvgMaxMs);
        OS::Delay(8000);
        check("R5 mag raw 10/200", mMagRaw, kMagTwenty);
        check("R5 mag raw 40/200", mMagRawSlow, kMagFive);
        check("R5 mag 40/200", mMag, kMagFive);
        checkRate("R5 mag raw 40 ms from 100 Hz", mMagRawSlow, kEvery40Ms);
        checkRate("R5 mag 40 ms from 100 Hz", mMag, kEvery40Ms);
        mMag.disconnect();
        mMagRawSlow.disconnect();
        mMagRaw.disconnect();
        OS::Delay(500);
    }

    /**
     * R6. The barometer as Floor and an activity app use it: 200 ms beside
     *     1000 ms. The part measures every 200 ms, so the 1000 ms listener
     *     gets every fifth measurement, 1 a second.
     */
    void r6()
    {
        mPress.connect(200, 200);
        mPressSlow.connect(1000, 1000);
        OS::Delay(1500);
        mPress.restartAll(kPressEach.gapMaxMs, kPressEach.oldestAvgMaxMs);
        mPressSlow.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R6 pressure 200/200", mPress, kPressEach);
        check("R6 pressure 1000/1000", mPressSlow, kPressSec);
        checkRate("R6 pressure 200 ms", mPress, kEvery200Ms);
        checkRate("R6 pressure 1000 ms from 200 ms", mPressSlow, kEvery1s);
        mPress.disconnect();
        mPressSlow.disconnect();
        OS::Delay(500);
    }

    /**
     * R7. The barometer at 100 ms with listeners that all ask for a latency
     *     of a second, so the measurements come ten times a second and the
     *     deliveries once: 10 samples a second for the 100 ms listener, one
     *     for the 1000 ms one. Then again with the deep sleep held off, to
     *     tell whether the spread of the stamps is the wake-up.
     */
    void r7()
    {
        mPressFast.connect(100, 1000);
        mPressSlow.connect(1000, 1000);
        OS::Delay(1500);
        mPressFast.restartAll(kPressTen.gapMaxMs, kPressTen.oldestAvgMaxMs);
        mPressSlow.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R7 pressure 100/1000", mPressFast, kPressTen);
        check("R7 pressure 1000/1000", mPressSlow, kPressSec);
        checkRate("R7 pressure 100 ms", mPressFast, kEvery100Ms);
        checkRate("R7 pressure 1000 ms from 100 ms", mPressSlow, kEvery1s);

        Driver::Sleep::Pause();
        OS::Delay(1500);
        mPressFast.restart(kPressTen.gapMaxMs, kPressTen.oldestAvgMaxMs);
        mPressSlow.restart(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R7 pressure 100/1000, no deep sleep", mPressFast, kPressTen);
        checkRate("R7 pressure 100 ms, no deep sleep", mPressFast, kEvery100MsAwake);
        checkRate("R7 pressure 1000 ms from 100 ms, no deep sleep", mPressSlow, kEvery1s);
        Driver::Sleep::Resume();

        mPressFast.disconnect();
        mPressSlow.disconnect();
        OS::Delay(500);
    }

    /**
     * R8. What is published on top of the pressure. The altimeter has no
     *     source of its own: it publishes one altitude for each pressure
     *     sample it is handed, so its rate is the pressure's, thinned to
     *     what was asked of it. A 1000 ms listener alone runs the part at
     *     1000 ms. A 200 ms listener joining it has to bring the part to
     *     200 ms through the altimeter, and then gets 5 a second while the
     *     first keeps its one. The temperature beside them is the other
     *     channel of the same measurements, one a second.
     */
    void r8()
    {
        mAlt.connect(1000, 1000);
        mTemp.connect(1000, 1000);
        OS::Delay(2500);
        mAlt.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        mTemp.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R8 altimeter 1000/1000", mAlt, kPressSec);
        check("R8 temperature 1000/1000", mTemp, kPressSec);
        checkRate("R8 altimeter 1000 ms", mAlt, kEvery1s);
        checkRate("R8 temperature 1000 ms", mTemp, kEvery1s);

        mAltFast.connect(200, 200);
        OS::Delay(2500);
        mAltFast.restartAll(kPressEach.gapMaxMs, kPressEach.oldestAvgMaxMs);
        mAlt.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        mTemp.restartAll(kPressSec.gapMaxMs, kPressSec.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R8 altimeter 200/200", mAltFast, kPressEach);
        check("R8 altimeter 1000/1000 beside 200", mAlt, kPressSec);
        checkRate("R8 altimeter 200 ms, joined a 1000 ms one", mAltFast, kEvery200Ms);
        checkRate("R8 altimeter 1000 ms from 200 ms", mAlt, kEvery1s);
        checkRate("R8 temperature 1000 ms from 200 ms", mTemp, kEvery1s);

        mAltFast.disconnect();
        mAlt.disconnect();
        mTemp.disconnect();
        OS::Delay(500);
    }

    /**
     * R9. A latency that is not a whole number of periods, on the parts
     *     that have no FIFO: 40 ms with 110 ms of it on the magnetometer and
     *     200 ms with 500 ms of it on the barometer, each alone.
     */
    void r9()
    {
        mMagOdd.connect(40, 110);
        OS::Delay(1500);
        mMagOdd.restartAll(kMagOdd.gapMaxMs, kMagOdd.oldestAvgMaxMs);
        OS::Delay(8000);
        check("R9 mag raw 40/110", mMagOdd, kMagOdd);
        checkRate("R9 mag raw 40 ms", mMagOdd, kEvery40Ms);
        mMagOdd.disconnect();
        OS::Delay(500);

        mPressOdd.connect(200, 500);
        OS::Delay(1500);
        mPressOdd.restartAll(kPressOdd.gapMaxMs, kPressOdd.oldestAvgMaxMs);
        OS::Delay(10000);
        check("R9 pressure 200/500", mPressOdd, kPressOdd);
        checkRate("R9 pressure 200 ms", mPressOdd, kEvery200Ms);
        mPressOdd.disconnect();
        OS::Delay(500);
    }

    Probe mSlow;
    Probe mRaw;
    Probe mSteps;
    Probe mGyro;
    Probe mGyroSlow;
    Probe mFusionRaw;
    Probe mFusionRawSlow;
    Probe mFusion;
    Probe mMagRaw;
    Probe mMagRawSlow;
    Probe mMag;
    Probe mMagOdd;
    Probe mPress;
    Probe mPressFast;
    Probe mPressSlow;
    Probe mPressOdd;
    Probe mAlt;
    Probe mAltFast;
    Probe mTemp;
};

void TestSensorRate_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    // Static: the probes of all the sources are more than the stack should carry.
    static TestSensorRate test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
