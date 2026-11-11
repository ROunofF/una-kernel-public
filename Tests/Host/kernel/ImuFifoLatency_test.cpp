/**
 * Host unit tests for the pieces that turn what is asked of the IMU into
 * BMI270 settings: the rate the part is set to for a data period, the read
 * period the IMU settles on for the streams that share the FIFO, and the
 * watermark that period comes to in bytes.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "Hardware/BMI270/Bmi270Odr.hpp"
#include "Hardware/BMI270/FifoWatermark.hpp"
#include "IMU/ImuFifoCadence.hpp"

namespace {

constexpr uint16_t kLimit = 2000;   // BMI270::kFifoWatermarkLimit

// A stream by a rate in whole Hz, 0 for off: the period the part runs at when
// asked for that rate's period. Whole rates of the part only - the fractional
// ones are written out as periods where they are tested.
uint32_t periodOf(uint16_t rateHz)
{
    if (rateHz == 0) {
        return 0;
    }

    return Hardware::Bmi270Odr::periodUs(Hardware::Bmi270Odr::fromPeriodUs(1000000u / rateHz));
}

uint16_t headerWatermark(uint16_t accelHz, uint16_t gyroHz, uint32_t periodMs)
{
    return Hardware::Bmi270Fifo::watermarkBytes(periodOf(accelHz), periodOf(gyroHz), periodMs, true, kLimit);
}

uint16_t plainWatermark(uint16_t accelHz, uint16_t gyroHz, uint32_t periodMs)
{
    return Hardware::Bmi270Fifo::watermarkBytes(periodOf(accelHz), periodOf(gyroHz), periodMs, false, kLimit);
}

} // namespace

// ---------------------------------------------------------------------------
// ImuFifoCadence::periodMs
// ---------------------------------------------------------------------------

TEST(ImuFifoCadence, NoRequestKeepsTheDefault)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 0, false, 0), 1000u);
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 0, true, 0), 1000u);
}

TEST(ImuFifoCadence, RequestIsHonoured)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 200, false, 0), 200u);
    EXPECT_EQ(ImuFifoCadence::periodMs(false, 0, true, 500), 500u);
}

TEST(ImuFifoCadence, SharedFifoFollowsTheTighterStream)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 1000, true, 200), 200u);
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 150, true, 600), 150u);
}

TEST(ImuFifoCadence, RequestBelowTheFloorIsHeldToIt)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 20, false, 0), ImuFifoCadence::kMinFifoLatencyMs);
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 1, true, 1), 100u);
}

TEST(ImuFifoCadence, RequestAboveTheDefaultIsHeldToIt)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(true, 5000, false, 0), ImuFifoCadence::kDefaultFifoLatencyMs);
}

// What a stream last asked for must not outlive it: the other stream would be
// read at a pace nobody wants any more.
TEST(ImuFifoCadence, StreamThatIsOffDoesNotCount)
{
    EXPECT_EQ(ImuFifoCadence::periodMs(false, 100, true, 0), 1000u);
    EXPECT_EQ(ImuFifoCadence::periodMs(false, 100, true, 400), 400u);
    EXPECT_EQ(ImuFifoCadence::periodMs(false, 100, false, 100), 1000u);
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Odr
// ---------------------------------------------------------------------------

TEST(Bmi270Odr, PeriodsAreExactAndDoubleDownTheRates)
{
    using namespace Hardware::Bmi270Odr;

    EXPECT_EQ(periodUs(0x0C), 625u);          // 1600 Hz
    EXPECT_EQ(periodUs(0x08), 10000u);        // 100 Hz
    EXPECT_EQ(periodUs(0x06), 40000u);        // 25 Hz
    EXPECT_EQ(periodUs(0x05), 80000u);        // 12.5 Hz, not 1e6 / 12
    EXPECT_EQ(periodUs(0x04), 160000u);       // 6.25 Hz
    EXPECT_EQ(periodUs(0x03), 320000u);       // 3.125 Hz
    EXPECT_EQ(periodUs(0x02), 640000u);       // 1.5625 Hz, not 1e6 / 1
    EXPECT_EQ(periodUs(0x01), 1280000u);      // 0.78125 Hz
}

TEST(Bmi270Odr, NotARateHasNoPeriod)
{
    EXPECT_EQ(Hardware::Bmi270Odr::periodUs(0x00), 0u);
    EXPECT_EQ(Hardware::Bmi270Odr::periodUs(0x0D), 0u);
}

TEST(Bmi270Odr, AskingForARatesOwnPeriodGivesThatRate)
{
    using namespace Hardware::Bmi270Odr;

    for (uint8_t odr = kSlowest; odr <= kFastest; ++odr) {
        EXPECT_EQ(fromPeriodUs(periodUs(odr)), odr) << "rate " << int(odr);
    }
}

// The part never gives readings less often than asked: 1000 ms falls between
// 1280 ms and 640 ms and gets 640 ms.
TEST(Bmi270Odr, RateIsNeverSlowerThanAskedFor)
{
    using namespace Hardware::Bmi270Odr;

    for (uint32_t wanted = 625; wanted <= 1400000; wanted += 137) {
        const uint8_t odr = fromPeriodUs(wanted);

        EXPECT_LE(periodUs(odr), wanted) << "asked for " << wanted;

        // And it is the slowest that fits: the next one down would be too slow.
        if (odr > kSlowest) {
            EXPECT_GT(periodUs(static_cast<uint8_t>(odr - 1)), wanted) << "asked for " << wanted;
        }
    }
}

TEST(Bmi270Odr, PeriodBetweenTwoRatesTakesTheFasterOne)
{
    using namespace Hardware::Bmi270Odr;

    EXPECT_EQ(periodUs(fromPeriodUs(1000000)), 640000u);    // 1 Hz asked: 1.5625 Hz, not 0.78
    EXPECT_EQ(periodUs(fromPeriodUs(500000)), 320000u);
    EXPECT_EQ(periodUs(fromPeriodUs(77000)), 40000u);       // 13 Hz asked: 25 Hz, not 12.5
    EXPECT_EQ(periodUs(fromPeriodUs(80000)), 80000u);
    EXPECT_EQ(periodUs(fromPeriodUs(30000)), 20000u);
    EXPECT_EQ(periodUs(fromPeriodUs(40000)), 40000u);
}

TEST(Bmi270Odr, RequestsBeyondEitherEndSaturate)
{
    using namespace Hardware::Bmi270Odr;

    EXPECT_EQ(fromPeriodUs(0), kFastest);
    EXPECT_EQ(fromPeriodUs(624), kFastest);
    EXPECT_EQ(fromPeriodUs(625), kFastest);
    EXPECT_EQ(fromPeriodUs(1280000), kSlowest);
    EXPECT_EQ(fromPeriodUs(5000000), kSlowest);
    EXPECT_EQ(fromPeriodUs(UINT32_MAX), kSlowest);
}

// The gyroscope has no rate under 25 Hz.
TEST(Bmi270Odr, GyroscopeStopsAt25Hz)
{
    using namespace Hardware::Bmi270Odr;

    EXPECT_EQ(periodUs(fromPeriodUs(1000000, kGyroSlowest)), 40000u);
    EXPECT_EQ(periodUs(fromPeriodUs(80000, kGyroSlowest)), 40000u);
    EXPECT_EQ(periodUs(fromPeriodUs(40000, kGyroSlowest)), 40000u);
    EXPECT_EQ(periodUs(fromPeriodUs(10000, kGyroSlowest)), 10000u);
}

// The features of the part need the accelerometer at 50 Hz or faster, and
// 25 Hz is asked for while one of them runs.
TEST(Bmi270Odr, FeaturesHoldTheAccelerometerToTheirRate)
{
    using namespace Hardware::Bmi270Odr;

    constexpr uint32_t kFeatureUs = 20000;

    EXPECT_EQ(periodUs(accelFromPeriodUs(40000, false, kFeatureUs)), 40000u);
    EXPECT_EQ(periodUs(accelFromPeriodUs(40000, true, kFeatureUs)), 20000u);
    EXPECT_EQ(periodUs(accelFromPeriodUs(1000000, true, kFeatureUs)), 20000u);
    EXPECT_EQ(periodUs(accelFromPeriodUs(20000, true, kFeatureUs)), 20000u);
}

TEST(Bmi270Odr, FeaturesNeverSlowAListenerDown)
{
    using namespace Hardware::Bmi270Odr;

    EXPECT_EQ(periodUs(accelFromPeriodUs(10000, true, 20000)), 10000u);
    EXPECT_EQ(periodUs(accelFromPeriodUs(625, true, 20000)), 625u);
    EXPECT_EQ(periodUs(accelFromPeriodUs(15000, true, 20000)), 10000u);
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Odr::accelSettingsValid / gyroSettingsValid
// ---------------------------------------------------------------------------

// What the part itself accepted and refused, rate by rate from 0.78125 Hz to
// 1600 Hz, when every combination was written to it on a watch.
TEST(Bmi270Odr, AccelerometerSettingsAsThePartTakesThem)
{
    using namespace Hardware::Bmi270Odr;

    static const char* const kPowerOptimised[8] = {
        "+++++++++++-",     // one sample averaged
        "++++++++++--",
        "+++++++++---",     // four
        "++++++++----",
        "+++++++-----",
        "++++++------",
        "+++++-------",
        "++++--------",     // 128
    };

    for (uint8_t averaging = 0; averaging < 8; ++averaging) {
        for (uint8_t odr = kSlowest; odr <= kFastest; ++odr) {
            EXPECT_EQ(accelSettingsValid(odr, false, averaging), kPowerOptimised[averaging][odr - kSlowest] == '+')
                << "averaging " << static_cast<int>(averaging) << " odr " << static_cast<int>(odr);

            // Performance-optimised, the averaging plays no part.
            EXPECT_EQ(accelSettingsValid(odr, true, averaging), "----++++++++"[odr - kSlowest] == '+')
                << "averaging " << static_cast<int>(averaging) << " odr " << static_cast<int>(odr);
        }
    }

    EXPECT_FALSE(accelSettingsValid(0, true, 0));
    EXPECT_FALSE(accelSettingsValid(kFastest + 1, true, 0));
}

TEST(Bmi270Odr, GyroscopeSettingsAsThePartTakesThem)
{
    using namespace Hardware::Bmi270Odr;

    // 25 Hz to 1600 Hz.
    for (uint8_t odr = kGyroSlowest; odr <= kFastest; ++odr) {
        EXPECT_EQ(gyroSettingsValid(odr, false), "+++----"[odr - kGyroSlowest] == '+') << static_cast<int>(odr);
        EXPECT_TRUE(gyroSettingsValid(odr, true)) << static_cast<int>(odr);
    }

    EXPECT_FALSE(gyroSettingsValid(kGyroSlowest - 1, true));
    EXPECT_FALSE(gyroSettingsValid(kFastest + 1, true));
}

// What the IMU component asks for is among what the part takes: the filter
// power-optimised with four samples averaged up to 200 Hz, performance-
// optimised above; the gyroscope power-optimised up to 100 Hz.
TEST(Bmi270Odr, TheSettingsInUseAreOnesThePartTakes)
{
    using namespace Hardware::Bmi270Odr;

    constexpr uint8_t kFourSamples = 2;

    for (uint8_t odr = kSlowest; odr <= kFastest; ++odr) {
        EXPECT_TRUE(accelSettingsValid(odr, periodUs(odr) < 5000, kFourSamples)) << static_cast<int>(odr);
    }

    for (uint8_t odr = kGyroSlowest; odr <= kFastest; ++odr) {
        EXPECT_TRUE(gyroSettingsValid(odr, periodUs(odr) < 10000)) << static_cast<int>(odr);
    }
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Fifo::watermarkBytes
// ---------------------------------------------------------------------------

// The slow rates are fractions of a Hz. Counted from a rate in whole Hz, a
// second at 12.5 Hz would hold 12 readings, at 1.5625 Hz one, and at
// 0.78125 Hz none at all - a watermark of zero.
TEST(Bmi270FifoWatermark, FractionalRatesAreCountedByTheirPeriods)
{
    using Hardware::Bmi270Fifo::watermarkBytes;

    EXPECT_EQ(watermarkBytes(80000, 0, 1000, true, kLimit), 12u * 7u);     // 12.5 Hz: 12.5 -> 12 whole frames
    EXPECT_EQ(watermarkBytes(80000, 0, 2000, true, kLimit), 25u * 7u);     // and 25 in two seconds, not 24
    EXPECT_EQ(watermarkBytes(160000, 0, 1000, true, kLimit), 6u * 7u);     // 6.25 Hz
    EXPECT_EQ(watermarkBytes(320000, 0, 1000, true, kLimit), 3u * 7u);     // 3.125 Hz
    EXPECT_EQ(watermarkBytes(640000, 0, 2000, true, kLimit), 3u * 7u);     // 1.5625 Hz: 3.1 -> 3
    EXPECT_EQ(watermarkBytes(1280000, 0, 1000, true, kLimit), 7u);         // 0.78125 Hz: not one in a second
    EXPECT_EQ(watermarkBytes(1280000, 0, 3000, true, kLimit), 2u * 7u);    // 2.3 -> 2
}


TEST(Bmi270FifoWatermark, NothingEnabledIsZero)
{
    EXPECT_EQ(headerWatermark(0, 0, 1000), 0u);
}

TEST(Bmi270FifoWatermark, SingleSensorHeaderFrames)
{
    EXPECT_EQ(headerWatermark(50, 0, 1000), 50u * 7u);
    EXPECT_EQ(headerWatermark(0, 100, 1000), 100u * 7u);
    EXPECT_EQ(headerWatermark(25, 0, 100), 2u * 7u);     // 2.5 readings -> 2
}

TEST(Bmi270FifoWatermark, BothSensorsAtOneRateShareFrames)
{
    EXPECT_EQ(headerWatermark(100, 100, 1000), 100u * 13u);
    EXPECT_EQ(headerWatermark(100, 100, 100), 10u * 13u);
}

TEST(Bmi270FifoWatermark, FasterSensorFillsTheRestWithItsOwnFrames)
{
    // 100 ms: 2 accel + 10 gyro readings -> 2 shared frames and 8 gyro frames,
    // 2 * 13 + 8 * 7 = 82 bytes, which is where a frame ends.
    EXPECT_EQ(headerWatermark(25, 100, 100), 82u);
    EXPECT_EQ(headerWatermark(100, 25, 100), 82u);
}

// A period shorter than the data period rounds to no readings at all. A level
// of zero never raises the interrupt, and the FIFO would never be read.
TEST(Bmi270FifoWatermark, NeverLessThanOneFrame)
{
    EXPECT_EQ(headerWatermark(25, 0, 20), 7u);
    EXPECT_EQ(headerWatermark(25, 25, 20), 13u);
    EXPECT_EQ(headerWatermark(50, 0, 0), 7u);
    EXPECT_EQ(plainWatermark(25, 0, 20), 6u);
    EXPECT_EQ(plainWatermark(25, 25, 20), 12u);
}

TEST(Bmi270FifoWatermark, HeldToTheLimitOfThePart)
{
    EXPECT_EQ(headerWatermark(1600, 0, 1000), kLimit);
    EXPECT_EQ(headerWatermark(1600, 1600, 1000), kLimit);
}

TEST(Bmi270FifoWatermark, HeaderlessPacksBothSensorsTogether)
{
    EXPECT_EQ(plainWatermark(50, 0, 1000), 50u * 6u);
    EXPECT_EQ(plainWatermark(50, 100, 1000), 100u * 12u);
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Fifo::limitWithReserve
// ---------------------------------------------------------------------------

// The level is held under the part's limit by what the streams produce while
// a read is late: 40 ms at 1600 Hz is 64 frames.
TEST(Bmi270FifoWatermark, ReserveIsWhatTheStreamsProduceInTheTime)
{
    using Hardware::Bmi270Fifo::limitWithReserve;

    EXPECT_EQ(limitWithReserve(periodOf(1600), 0, 40, true, kLimit), kLimit - 64u * 7u);
    EXPECT_EQ(limitWithReserve(0, periodOf(1600), 40, true, kLimit), kLimit - 64u * 7u);
    EXPECT_EQ(limitWithReserve(periodOf(1600), periodOf(1600), 40, true, kLimit), kLimit - 64u * 13u);
    EXPECT_EQ(limitWithReserve(periodOf(100), 0, 40, true, kLimit), kLimit - 4u * 7u);

    // One shared frame and 63 of the faster stream's own.
    EXPECT_EQ(limitWithReserve(periodOf(1600), periodOf(25), 40, true, kLimit), kLimit - (13u + 63u * 7u));
}

// A stream slower than the reserve still has a frame on its way.
TEST(Bmi270FifoWatermark, ReserveIsNeverLessThanOneFrame)
{
    using Hardware::Bmi270Fifo::limitWithReserve;

    EXPECT_EQ(limitWithReserve(1280000, 0, 40, true, kLimit), kLimit - 7u);
    EXPECT_EQ(limitWithReserve(periodOf(25), periodOf(25), 20, true, kLimit), kLimit - 13u);
}

TEST(Bmi270FifoWatermark, NoReserveLeavesTheLimitOfThePart)
{
    using Hardware::Bmi270Fifo::limitWithReserve;

    EXPECT_EQ(limitWithReserve(periodOf(1600), periodOf(1600), 0, true, kLimit), kLimit);
    EXPECT_EQ(limitWithReserve(0, 0, 40, true, kLimit), kLimit);
}

// A reserve the limit cannot hold still leaves a level that raises the
// interrupt: one frame.
TEST(Bmi270FifoWatermark, ReserveLeavesAtLeastOneFrameOfLevel)
{
    using Hardware::Bmi270Fifo::limitWithReserve;

    EXPECT_EQ(limitWithReserve(periodOf(1600), periodOf(1600), 1000, true, kLimit), 13u);
    EXPECT_EQ(limitWithReserve(periodOf(1600), 0, 40, true, 10), 7u);
}

// The heaviest load: the watermark a second's request comes to with the
// reserve kept, and the room that leaves in the FIFO.
TEST(Bmi270FifoWatermark, ReserveCutsTheWatermarkOfAFastStream)
{
    using Hardware::Bmi270Fifo::limitWithReserve;
    using Hardware::Bmi270Fifo::watermarkBytes;

    const uint32_t fast  = periodOf(1600);
    const uint16_t limit = limitWithReserve(fast, fast, ImuFifoCadence::kReadReserveMs, true, kLimit);

    EXPECT_EQ(watermarkBytes(fast, fast, 1000, true, limit), 1168u);
    EXPECT_EQ(watermarkBytes(periodOf(25), 0, 1000, true,
                             limitWithReserve(periodOf(25), 0, ImuFifoCadence::kReadReserveMs, true, kLimit)),
              25u * 7u);
}

// End to end: a stream at 20 ms asked for at a latency of 100 ms.
TEST(Bmi270FifoWatermark, RequestedLatencyReachesTheWatermark)
{
    const uint32_t period = ImuFifoCadence::periodMs(true, 100, false, 0);

    EXPECT_EQ(period, 100u);
    EXPECT_EQ(headerWatermark(50, 0, period), 5u * 7u);
}
