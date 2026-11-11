/**
 * Host unit tests for Ble::ProfileCsc -- the Cycling Speed and Cadence Service
 * (0x1816 / 0x2A5B) parser. Pure logic, no BlueNRG/OS deps.
 *
 * The wheel payloads are REAL BYTES captured from a Garmin Speed Sensor 2,
 * used in preference to invented frames so the layout is checked against a
 * device rather than against our reading of the spec.
 */

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/ProfileCsc.hpp"

namespace {

using Ble::AccessorySample;
using Ble::ProfileCsc;
using Ble::RevTickRate;

// A char-by-uuid declaration value: properties | valueHandle(LE) | uuid16(LE).
std::array<uint8_t, 5> declValue(uint16_t valueHandle, uint16_t uuid16)
{
    return { 0x10,
             static_cast<uint8_t>(valueHandle & 0xFF),
             static_cast<uint8_t>(valueHandle >> 8),
             static_cast<uint8_t>(uuid16 & 0xFF),
             static_cast<uint8_t>(uuid16 >> 8) };
}

} // namespace

TEST(ProfileCsc, AdvertisesCscUuids)
{
    ProfileCsc p;
    EXPECT_EQ(p.serviceUuid(), 0x1816);
    EXPECT_EQ(p.characteristicUuid(), 0x2A5B);
    EXPECT_EQ(p.valueHandle(), 0);
    EXPECT_EQ(p.cccdHandle(), 0);
}

TEST(ProfileCsc, DiscoveryRecordsValueAndCccdHandles)
{
    ProfileCsc p;
    auto v = declValue(0x0031, 0x2A5B);
    EXPECT_TRUE(p.onCharByUuidResp(0x0030, v.data(), static_cast<uint8_t>(v.size())));
    EXPECT_EQ(p.valueHandle(), 0x0031);
    EXPECT_EQ(p.cccdHandle(), 0x0032);  // value + 1
}

TEST(ProfileCsc, DiscoveryRejectsWrongUuidAndShortData)
{
    ProfileCsc p;
    auto other = declValue(0x0031, 0x2A5C);  // CSC Feature, not Measurement
    EXPECT_FALSE(p.onCharByUuidResp(0x0030, other.data(),
            static_cast<uint8_t>(other.size())));
    EXPECT_EQ(p.valueHandle(), 0);

    uint8_t truncated[4] = { 0x10, 0x31, 0x00, 0x5B };
    EXPECT_FALSE(p.onCharByUuidResp(0x0030, truncated, sizeof(truncated)));
    EXPECT_EQ(p.valueHandle(), 0);
}

TEST(ProfileCsc, ParsesCapturedGarminWheelOnlyFrame)
{
    // 01-10-02-00-00-C3-2E : flags 0x01 (wheel only), 528 revs, 11971 ticks.
    ProfileCsc p;
    uint8_t notif[7] = { 0x01, 0x10, 0x02, 0x00, 0x00, 0xC3, 0x2E };
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));

    EXPECT_EQ(s.kind, AccessorySample::Kind::CyclingRevs);
    EXPECT_EQ(s.fields, AccessorySample::FieldWheel);
    EXPECT_EQ(s.wheelRevs, 528u);
    EXPECT_EQ(s.wheelEventTicks, 11971u);
    EXPECT_EQ(s.wheelTickRate, RevTickRate::Hz1024);  // CSC, not CPS's 1/2048 s
    EXPECT_EQ(s.crankRevs, 0u);                       // absent, left at default
    EXPECT_EQ(s.powerWatts, 0);                       // CSC never carries power
    EXPECT_EQ(s.trust, 3);
}

TEST(ProfileCsc, ParsesCrankOnlyFrame)
{
    // Garmin Cadence Sensor 2 shape: flags 0x02, 5 bytes total.
    // 1000 revs (0x03E8), 2048 ticks (0x0800).
    ProfileCsc p;
    uint8_t notif[5] = { 0x02, 0xE8, 0x03, 0x00, 0x08 };
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));

    EXPECT_EQ(s.fields, AccessorySample::FieldCrank);
    EXPECT_EQ(s.crankRevs, 1000u);
    EXPECT_EQ(s.crankEventTicks, 2048u);
    EXPECT_EQ(s.wheelRevs, 0u);
}

TEST(ProfileCsc, ParsesBothFieldsInFlagBitOrder)
{
    // flags 0x03: wheel block (u32+u16) THEN crank block (u16+u16), 11 bytes.
    ProfileCsc p;
    uint8_t notif[11] = { 0x03,
                          0x10, 0x02, 0x00, 0x00,   // wheel revs 528
                          0xC3, 0x2E,               // wheel ticks 11971
                          0xE8, 0x03,               // crank revs 1000
                          0x00, 0x08 };             // crank ticks 2048
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));

    EXPECT_EQ(s.fields, AccessorySample::FieldWheel | AccessorySample::FieldCrank);
    EXPECT_EQ(s.wheelRevs, 528u);
    EXPECT_EQ(s.wheelEventTicks, 11971u);
    EXPECT_EQ(s.crankRevs, 1000u);
    EXPECT_EQ(s.crankEventTicks, 2048u);
}

TEST(ProfileCsc, RejectsFrameWithNoFieldsPresent)
{
    // Well-formed but carries no measurement: drop rather than publish empty.
    ProfileCsc p;
    uint8_t notif[1] = { 0x00 };
    AccessorySample s;
    EXPECT_FALSE(p.parseNotification(0, notif, sizeof(notif), s));
}

TEST(ProfileCsc, RejectsTruncatedFrames)
{
    ProfileCsc p;
    AccessorySample s;

    EXPECT_FALSE(p.parseNotification(0, nullptr, 0, s));       // no data

    uint8_t empty[1] = { 0x01 };
    EXPECT_FALSE(p.parseNotification(0, empty, 0, s));         // zero length

    uint8_t wheelShort[6] = { 0x01, 0x10, 0x02, 0x00, 0x00, 0xC3 };  // 1 byte short
    EXPECT_FALSE(p.parseNotification(0, wheelShort, sizeof(wheelShort), s));

    uint8_t crankShort[4] = { 0x02, 0xE8, 0x03, 0x00 };        // 1 byte short
    EXPECT_FALSE(p.parseNotification(0, crankShort, sizeof(crankShort), s));

    // Both flags set but only the wheel block supplied.
    uint8_t bothShort[7] = { 0x03, 0x10, 0x02, 0x00, 0x00, 0xC3, 0x2E };
    EXPECT_FALSE(p.parseNotification(0, bothShort, sizeof(bothShort), s));
}

TEST(ProfileCsc, IgnoresUnknownFlagBits)
{
    // Reserved bits 2-7 must not change how the known fields decode.
    ProfileCsc p;
    uint8_t notif[5] = { 0xFE & ~0x01, 0xE8, 0x03, 0x00, 0x08 };  // crank + reserved
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.crankRevs, 1000u);
}

TEST(ProfileCsc, ToleratesTrailingBytes)
{
    // A longer-than-expected frame is not a reason to reject: we read only what
    // the flags describe and ignore the rest.
    ProfileCsc p;
    uint8_t notif[9] = { 0x02, 0xE8, 0x03, 0x00, 0x08, 0xAA, 0xBB, 0xCC, 0xDD };
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.crankRevs, 1000u);
    EXPECT_EQ(s.crankEventTicks, 2048u);
}

TEST(ProfileCsc, IgnoresNotificationsForOtherHandlesAfterDiscovery)
{
    ProfileCsc p;
    auto v = declValue(0x0031, 0x2A5B);
    ASSERT_TRUE(p.onCharByUuidResp(0x0030, v.data(), static_cast<uint8_t>(v.size())));

    uint8_t notif[5] = { 0x02, 0xE8, 0x03, 0x00, 0x08 };
    AccessorySample s;
    EXPECT_FALSE(p.parseNotification(0x0099, notif, sizeof(notif), s));
    EXPECT_TRUE(p.parseNotification(0x0031, notif, sizeof(notif), s));
}

TEST(ProfileCsc, ResetClearsHandles)
{
    ProfileCsc p;
    auto v = declValue(0x0031, 0x2A5B);
    ASSERT_TRUE(p.onCharByUuidResp(0x0030, v.data(), static_cast<uint8_t>(v.size())));
    p.reset();
    EXPECT_EQ(p.valueHandle(), 0);
    EXPECT_EQ(p.cccdHandle(), 0);
}
