/**
 ******************************************************************************
 * @file    MS5837_test.cpp
 * @brief   Host tests for the MS5837 hardware driver: compensation math, PROM
 *          CRC / model gating, and ADC-word sanity (A3/B7 hardening).
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <ios>

#include "MS5837.hpp"

#include "FakeMs5837.hpp"

using Hardware::MS5837;

using FakeI2c = FakeMs5837::Bus;
using FakeMs5837::makeProm;
using FakeMs5837::kFactory02BA01;
using FakeMs5837::kFactory02BA21;
using FakeMs5837::kFactory30BA26;
using FakeMs5837::kFactoryUnknown;

// --- CRC4 against the datasheet reference vector -----------------------------

TEST(MS5837, Crc4LoopMatchesAn520Vector)
{
    // AN520 worked example (MS56xx family, CRC in word 7): the image
    // {0x3132..0x4546} with word 7's low byte cleared to 0x4500 yields CRC 0x0B.
    // This pins the CRC *arithmetic* against the datasheet, independent of
    // makeProm()/getPROM(). The MS5837-specific preparation Crc4 is fed with
    // (CRC in word0's top nibble, word7 zeroed) is exercised by the round-trip
    // tests InitAndGetSucceedOnAValidPart / InitRejectsBadCrc.
    uint16_t v[8] = {0x3132, 0x3334, 0x3536, 0x3738,
                     0x3940, 0x4142, 0x4344, 0x4500};
    EXPECT_EQ(MS5837::Crc4(v), 0x0Bu);
}

TEST(MS5837, Crc4OfAllZerosIsZero)
{
    // This CRC-4 uses a zero init value and no final XOR, so the shift register
    // starts at 0 and all-zero input never perturbs it -> CRC == 0. That is the
    // blind spot the separate all-zero-PROM guard covers; the CRC alone cannot.
    uint16_t v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(MS5837::Crc4(v), 0x00u);
}

// --- A3: ADC-word sanity -----------------------------------------------------

TEST(MS5837, RejectsDegenerateAdcWords)
{
    EXPECT_FALSE(MS5837::isValidAdc(0x000000));   // conversion not ready
    EXPECT_FALSE(MS5837::isValidAdc(0xFFFFFF));   // bus stuck high
    EXPECT_FALSE(MS5837::isValidAdc(0x1000000));  // outside the 24-bit range
    EXPECT_TRUE(MS5837::isValidAdc(0x000001));
    EXPECT_TRUE(MS5837::isValidAdc(0x800000));
    EXPECT_TRUE(MS5837::isValidAdc(0xFFFFFE));
}

// --- B7: model gating --------------------------------------------------------

TEST(MS5837, GetModelMatchesDatasheetVersionCodes)
{
    EXPECT_EQ(MS5837::getModel(makeProm(kFactory02BA01)),  MS5837::Model::MS5837_02BA01);
    EXPECT_EQ(MS5837::getModel(makeProm(kFactory02BA21)),  MS5837::Model::MS5837_02BA21);
    EXPECT_EQ(MS5837::getModel(makeProm(kFactory30BA26)),  MS5837::Model::MS5837_30BA26);
    EXPECT_EQ(MS5837::getModel(makeProm(kFactoryUnknown)), MS5837::Model::MS5837_UNRECOGNISED);
}

TEST(MS5837, OnlyThe02BA01BoardPartIsSupported)
{
    // The board carries the 02BA01 only, so anything else - even the 02BA21,
    // which shares the 02BA math - must be rejected as a misdetected/foreign
    // part rather than converted.
    EXPECT_TRUE(MS5837::isSupportedModel(makeProm(kFactory02BA01)));
    EXPECT_FALSE(MS5837::isSupportedModel(makeProm(kFactory02BA21)));
    EXPECT_FALSE(MS5837::isSupportedModel(makeProm(kFactory30BA26)));
    EXPECT_FALSE(MS5837::isSupportedModel(makeProm(kFactoryUnknown)));
}

TEST(MS5837, IsSupportedModelRejectsAllZeroProm)
{
    // The public validation surface must give the same answer as init()'s
    // internal path: an all-zero image reads as version 0x00 = 02BA01 but is a
    // dead/foreign part, so a caller that skips getPROM() (bring-up, recovery,
    // tests) cannot be tricked into feeding it to calculate().
    MS5837::PROM zero {};
    EXPECT_FALSE(MS5837::isSupportedModel(zero));
}

// --- Compensation math -------------------------------------------------------

TEST(MS5837, TemperatureIsTwentyWhenDeltaIsZero)
{
    // dT = d2 - c5*256 == 0  =>  TEMP = 2000 centi-degC  =>  20.00 C exactly.
    const uint16_t c5 = 30000;
    MS5837::PROM p = makeProm(kFactory02BA01, 40000, 38000, 25000, 22000, c5, 26000);

    const uint32_t d2 = static_cast<uint32_t>(c5) * 256u;

    float t = -1.0f, pr = -1.0f;
    MS5837::calculate(p, /*d1*/0x900000, d2, t, pr);
    EXPECT_FLOAT_EQ(t, 20.0f);
}

TEST(MS5837, TemperatureIsMonotonicInD2)
{
    const uint16_t c5 = 30000;
    MS5837::PROM p = makeProm(kFactory02BA01, 40000, 38000, 25000, 22000, c5, 26000);
    const uint32_t base = static_cast<uint32_t>(c5) * 256u;

    float tLow = 0.0f, tHigh = 0.0f, pr = 0.0f;
    MS5837::calculate(p, 0x900000, base,             tLow,  pr);
    MS5837::calculate(p, 0x900000, base + 1000000u,  tHigh, pr);
    EXPECT_GT(tHigh, tLow);
}

// --- Integration through the I2C path ---------------------------------------

TEST(MS5837, InitAndGetSucceedOnAValidPart)
{
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    ASSERT_TRUE(dev.init());

    float t = 0.0f, p = 0.0f;
    EXPECT_TRUE(dev.get(t, p));
    EXPECT_GT(t, -60.0f);
    EXPECT_LT(t,  90.0f);
}

// The words a caller compares to tell a fresh conversion from a repeat.
TEST(MS5837, GetReportsTheAdcWordsItConverted)
{
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);
    bus.d1   = 0x912345;
    bus.d2   = 0x8ABCDE;

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    ASSERT_TRUE(dev.init());

    float t = 0.0f, p = 0.0f;
    MS5837::Raw raw;
    ASSERT_TRUE(dev.get(t, p, raw));
    EXPECT_EQ(raw.d1, 0x912345u);
    EXPECT_EQ(raw.d2, 0x8ABCDEu);

    // The same values as the call that does not report them.
    float t2 = 0.0f, p2 = 0.0f;
    ASSERT_TRUE(dev.get(t2, p2));
    EXPECT_FLOAT_EQ(t2, t);
    EXPECT_FLOAT_EQ(p2, p);
}

// A caller keeps the last pair to compare with; a failed read must not
// hand it half of a new one.
TEST(MS5837, AFailedGetLeavesTheAdcWordsUntouched)
{
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);
    bus.d2   = 0xFFFFFF;   // the second conversion is refused as invalid

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    ASSERT_TRUE(dev.init());

    float t = 0.0f, p = 0.0f;
    MS5837::Raw raw;
    raw.d1 = 0x111111;
    raw.d2 = 0x222222;

    EXPECT_FALSE(dev.get(t, p, raw));
    EXPECT_EQ(raw.d1, 0x111111u);
    EXPECT_EQ(raw.d2, 0x222222u);
}

TEST(MS5837, InitRejectsBadCrc)
{
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);
    bus.prom.raw[3] ^= 0x0001;   // corrupt a coefficient, CRC nibble now stale

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    EXPECT_FALSE(dev.init());
}

TEST(MS5837, InitRejectsModelsOtherThan02BA01)
{
    // Each has a valid CRC but is not the board part - init must reject them so
    // no wrong-model pressure is ever produced.
    for (uint8_t factory : {kFactory02BA21, kFactory30BA26, kFactoryUnknown}) {
        FakeI2c bus;
        bus.prom = makeProm(factory);

        MS5837 dev;
        ASSERT_TRUE(dev.config(bus));
        EXPECT_FALSE(dev.init()) << "factory code 0x" << std::hex << int(factory);
    }
}

TEST(MS5837, ConstructorHonoursDeviceAddress)
{
    FakeI2c bus;
    const uint8_t customAddr = 0x77;

    MS5837 dev(bus, customAddr);
    dev.isReady();                       // drives isDeviceReady(mAddr)
    EXPECT_EQ(bus.lastAddr, customAddr); // must be the ctor arg, not the default
}

TEST(MS5837, InitRejectsAllZeroProm)
{
    // A dead/foreign device that ACKs but returns 0x0000 for every PROM word
    // passes the CRC trivially (CRC of zero is zero) and reads as version
    // 0x00 = 02BA01. It must still be rejected: a real part never has all-zero
    // calibration.
    FakeI2c bus;                 // prom is value-initialised to all zeros
    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    EXPECT_FALSE(dev.init());
}

TEST(MS5837, GetFailsOnStuckBusAdcWord)
{
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);
    bus.d2   = 0xFFFFFF;   // D2 read comes back all-ones (SDA stuck high)

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    ASSERT_TRUE(dev.init());

    float t = 0.0f, p = 0.0f;
    EXPECT_FALSE(dev.get(t, p));
}

TEST(MS5837, GetRefusesBeforeInit)
{
    // A good part is on the bus, but init() was never called. get() must refuse
    // rather than fabricate 20.00 C / 0.00 mbar from the value-initialised mPROM.
    FakeI2c bus;
    bus.prom = makeProm(kFactory02BA01);

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));

    float t = -1.0f, p = -1.0f;
    EXPECT_FALSE(dev.get(t, p));
}

TEST(MS5837, GetRefusesAfterRejectedInit)
{
    // init() rejects a valid-CRC but wrong-model part; mPROM now holds that
    // part's real coefficients. get() must stay refused so the 02BA math is
    // never applied to them - the wrong-model conversion this driver prevents.
    FakeI2c bus;
    bus.prom = makeProm(kFactory30BA26);

    MS5837 dev;
    ASSERT_TRUE(dev.config(bus));
    ASSERT_FALSE(dev.init());

    float t = -1.0f, p = -1.0f;
    EXPECT_FALSE(dev.get(t, p));
}
