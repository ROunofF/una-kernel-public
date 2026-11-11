/**
 * Host unit tests for Ble::ProfileCps -- the Cycling Power Service
 * (0x1818 / 0x2A63) parser. Pure logic, no BlueNRG/OS deps.
 *
 * The emphasis is deliberately on FLAG PERMUTATIONS rather than happy-path
 * decoding. CPS puts two optional fields (pedal power balance, accumulated
 * torque) BEFORE the wheel/crank data, so a parser that fails to step over them
 * reads the counters from the wrong offset and produces plausible-looking
 * nonsense instead of an error -- the failure mode most likely to reach a rider.
 *
 * One frame matches the ESP32 bench simulator's output byte for byte, since that
 * is the only CPS peripheral we can currently exercise on hardware.
 */

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/ProfileCps.hpp"

namespace {

using Ble::AccessorySample;
using Ble::ProfileCps;
using Ble::RevTickRate;

std::array<uint8_t, 5> declValue(uint16_t valueHandle, uint16_t uuid16)
{
    return { 0x10,
             static_cast<uint8_t>(valueHandle & 0xFF),
             static_cast<uint8_t>(valueHandle >> 8),
             static_cast<uint8_t>(uuid16 & 0xFF),
             static_cast<uint8_t>(uuid16 >> 8) };
}

void pushU16(std::vector<uint8_t> &v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void pushU32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) {
        v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
    }
}

// Build a Cycling Power Measurement frame with fields in spec (flag-bit) order.
struct Frame {
    uint16_t flags = 0;
    int16_t  power = 0;
    uint8_t  balance = 0;            // bit 0
    uint16_t torque = 0;             // bit 2
    uint32_t wheelRevs = 0;          // bit 4
    uint16_t wheelTicks = 0;
    uint16_t crankRevs = 0;          // bit 5
    uint16_t crankTicks = 0;
    bool     trailingExtras = false; // bits 6+ payload we must ignore

    std::vector<uint8_t> bytes() const
    {
        std::vector<uint8_t> v;
        pushU16(v, flags);
        pushU16(v, static_cast<uint16_t>(power));
        if (flags & 0x0001) { v.push_back(balance); }
        if (flags & 0x0004) { pushU16(v, torque); }
        if (flags & 0x0010) { pushU32(v, wheelRevs); pushU16(v, wheelTicks); }
        if (flags & 0x0020) { pushU16(v, crankRevs); pushU16(v, crankTicks); }
        if (trailingExtras) {
            // e.g. extreme force magnitudes: two s16. Never read by us.
            pushU16(v, 0x1111);
            pushU16(v, 0x2222);
        }
        return v;
    }
};

bool parse(ProfileCps &p, const Frame &f, AccessorySample &out)
{
    const std::vector<uint8_t> b = f.bytes();
    return p.parseNotification(0, b.data(), static_cast<uint8_t>(b.size()), out);
}

} // namespace

TEST(ProfileCps, AdvertisesCpsUuids)
{
    ProfileCps p;
    EXPECT_EQ(p.serviceUuid(), 0x1818);
    EXPECT_EQ(p.characteristicUuid(), 0x2A63);
}

TEST(ProfileCps, DiscoveryRecordsValueAndCccdHandles)
{
    ProfileCps p;
    auto v = declValue(0x0025, 0x2A63);
    EXPECT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));
    EXPECT_EQ(p.valueHandle(), 0x0025);
    EXPECT_EQ(p.cccdHandle(), 0x0026);
}

TEST(ProfileCps, DiscoveryRejectsFeatureCharacteristic)
{
    // 0x2A65 is Cycling Power Feature, which we deliberately never read.
    ProfileCps p;
    auto v = declValue(0x0025, 0x2A65);
    EXPECT_FALSE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));
    EXPECT_EQ(p.valueHandle(), 0);
}

TEST(ProfileCps, PowerOnlyIsAValidFrame)
{
    // Minimum legal frame: flags + power, 4 bytes. A power meter with no
    // optional fields is fully compliant.
    ProfileCps p;
    AccessorySample s;
    Frame f; f.flags = 0x0000; f.power = 250;
    ASSERT_TRUE(parse(p, f, s));

    EXPECT_EQ(s.kind, AccessorySample::Kind::CyclingPower);
    EXPECT_EQ(s.fields, AccessorySample::FieldPower);
    EXPECT_EQ(s.powerWatts, 250);
    EXPECT_EQ(s.trust, 3);
}

TEST(ProfileCps, PowerIsSigned)
{
    // Coasting on some trainers reports a small negative value; an unsigned read
    // would turn -5 W into 65531 W.
    ProfileCps p;
    AccessorySample s;
    Frame f; f.power = -5;
    ASSERT_TRUE(parse(p, f, s));
    EXPECT_EQ(s.powerWatts, -5);
}

TEST(ProfileCps, ParsesEsp32SimulatorFrameByteForByte)
{
    // The bench sim sets flags 0x0030 (wheel bit 4 | crank bit 5) and writes
    // flags, power, wheel u32 + u16, crank u16 + u16 -- 14 bytes.
    ProfileCps p;
    uint8_t notif[14] = { 0x30, 0x00,               // flags 0x0030
                          0x96, 0x00,               // power 150 W
                          0x40, 0x06, 0x00, 0x00,   // wheel revs 1600
                          0x00, 0x40,               // wheel ticks 16384
                          0x64, 0x00,               // crank revs 100
                          0x00, 0x20 };             // crank ticks 8192
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));

    EXPECT_EQ(s.powerWatts, 150);
    EXPECT_EQ(s.wheelRevs, 1600u);
    EXPECT_EQ(s.wheelEventTicks, 16384u);
    EXPECT_EQ(s.crankRevs, 100u);
    EXPECT_EQ(s.crankEventTicks, 8192u);
    EXPECT_EQ(s.fields, AccessorySample::FieldPower | AccessorySample::FieldWheel
                        | AccessorySample::FieldCrank);
}

TEST(ProfileCps, WheelTickRateIs2048UnlikeCsc)
{
    // Getting this wrong scales the derived wheel rate by exactly two.
    ProfileCps p;
    AccessorySample s;
    Frame f; f.flags = 0x0010; f.wheelRevs = 10; f.wheelTicks = 2048;
    ASSERT_TRUE(parse(p, f, s));
    EXPECT_EQ(s.wheelTickRate, RevTickRate::Hz2048);
}

TEST(ProfileCps, SkipsPedalPowerBalanceBeforeCrankData)
{
    // THE core trap. flags 0x0021 = balance (bit 0) + crank (bit 5). A parser
    // that does not step over the 1-byte balance field reads the crank counter
    // one byte early and returns garbage that still looks like a plausible rpm.
    ProfileCps p;
    AccessorySample s;
    Frame f;
    f.flags = 0x0021;
    f.power = 200;
    f.balance = 50;
    f.crankRevs = 0x1234;
    f.crankTicks = 0x5678;
    ASSERT_TRUE(parse(p, f, s));

    EXPECT_EQ(s.crankRevs, 0x1234u);
    EXPECT_EQ(s.crankEventTicks, 0x5678u);
    EXPECT_EQ(s.powerWatts, 200);
    // The balance byte must NOT have leaked into any reported field.
    EXPECT_EQ(s.fields, AccessorySample::FieldPower | AccessorySample::FieldCrank);
}

TEST(ProfileCps, SkipsAccumulatedTorqueBeforeWheelData)
{
    // flags 0x0014 = accumulated torque (bit 2) + wheel (bit 4).
    ProfileCps p;
    AccessorySample s;
    Frame f;
    f.flags = 0x0014;
    f.torque = 0xBEEF;
    f.wheelRevs = 0xDEADBEEF;
    f.wheelTicks = 0xCAFE;
    ASSERT_TRUE(parse(p, f, s));

    EXPECT_EQ(s.wheelRevs, 0xDEADBEEFu);
    EXPECT_EQ(s.wheelEventTicks, 0xCAFEu);
}

TEST(ProfileCps, SkipsBothPrecedingFieldsTogether)
{
    // flags 0x0035 = balance | torque | wheel | crank: every skip and read in
    // one frame, which is where an off-by-one compounds.
    ProfileCps p;
    AccessorySample s;
    Frame f;
    f.flags = 0x0035;
    f.power = 300;
    f.balance = 51;
    f.torque = 0x0102;
    f.wheelRevs = 0x00010203;
    f.wheelTicks = 0x0405;
    f.crankRevs = 0x0607;
    f.crankTicks = 0x0809;
    ASSERT_TRUE(parse(p, f, s));

    EXPECT_EQ(s.powerWatts, 300);
    EXPECT_EQ(s.wheelRevs, 0x00010203u);
    EXPECT_EQ(s.wheelEventTicks, 0x0405u);
    EXPECT_EQ(s.crankRevs, 0x0607u);
    EXPECT_EQ(s.crankEventTicks, 0x0809u);
}

TEST(ProfileCps, IgnoresTrailingOptionalFields)
{
    // Bits 6+ (extreme force/torque, angles, dead spots, energy) sit after the
    // crank block. We never read them, so their presence must not matter.
    ProfileCps p;
    AccessorySample s;
    Frame f;
    f.flags = 0x0060;          // crank (bit 5) + extreme force magnitudes (bit 6)
    f.crankRevs = 777;
    f.crankTicks = 1024;
    f.trailingExtras = true;
    ASSERT_TRUE(parse(p, f, s));
    EXPECT_EQ(s.crankRevs, 777u);
    EXPECT_EQ(s.crankEventTicks, 1024u);
}

TEST(ProfileCps, RejectsTruncatedFrames)
{
    ProfileCps p;
    AccessorySample s;

    EXPECT_FALSE(p.parseNotification(0, nullptr, 0, s));

    uint8_t flagsOnly[2] = { 0x00, 0x00 };            // no power
    EXPECT_FALSE(p.parseNotification(0, flagsOnly, sizeof(flagsOnly), s));

    uint8_t three[3] = { 0x00, 0x00, 0x64 };          // power half-present
    EXPECT_FALSE(p.parseNotification(0, three, sizeof(three), s));

    // Claims wheel data but supplies 5 of the 6 bytes.
    uint8_t wheelShort[9] = { 0x10, 0x00, 0x64, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF };
    EXPECT_FALSE(p.parseNotification(0, wheelShort, sizeof(wheelShort), s));

    // Claims balance but the frame ends immediately after power: the SKIP must
    // fail, not silently succeed.
    uint8_t balanceMissing[4] = { 0x01, 0x00, 0x64, 0x00 };
    EXPECT_FALSE(p.parseNotification(0, balanceMissing, sizeof(balanceMissing), s));
}

TEST(ProfileCps, IgnoresNotificationsForOtherHandlesAfterDiscovery)
{
    ProfileCps p;
    auto v = declValue(0x0025, 0x2A63);
    ASSERT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));

    uint8_t notif[4] = { 0x00, 0x00, 0x64, 0x00 };
    AccessorySample s;
    EXPECT_FALSE(p.parseNotification(0x0099, notif, sizeof(notif), s));
    EXPECT_TRUE(p.parseNotification(0x0025, notif, sizeof(notif), s));
}

TEST(ProfileCps, ResetClearsHandles)
{
    ProfileCps p;
    auto v = declValue(0x0025, 0x2A63);
    ASSERT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));
    p.reset();
    EXPECT_EQ(p.valueHandle(), 0);
    EXPECT_EQ(p.cccdHandle(), 0);
}
