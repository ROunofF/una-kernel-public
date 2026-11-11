/**
 * Host unit tests for Ble::ProfileHrs — the Heart Rate Service (0x180D /
 * 0x2A37) accessory profile parser. Pure logic, no BlueNRG/OS deps.
 */

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/ProfileHrs.hpp"

namespace {

using Ble::AccessorySample;
using Ble::ProfileHrs;

// A char-by-uuid declaration value: properties | valueHandle(LE) | uuid16(LE).
// Helper builds one for value handle 0x0025 and the given 16-bit UUID.
std::array<uint8_t, 5> declValue(uint16_t valueHandle, uint16_t uuid16)
{
    return { 0x10,
             static_cast<uint8_t>(valueHandle & 0xFF),
             static_cast<uint8_t>(valueHandle >> 8),
             static_cast<uint8_t>(uuid16 & 0xFF),
             static_cast<uint8_t>(uuid16 >> 8) };
}

} // namespace

TEST(ProfileHrs, AdvertisesHrsUuids)
{
    ProfileHrs p;
    EXPECT_EQ(p.serviceUuid(), 0x180D);
    EXPECT_EQ(p.characteristicUuid(), 0x2A37);
    EXPECT_EQ(p.valueHandle(), 0);
    EXPECT_EQ(p.cccdHandle(), 0);
}

TEST(ProfileHrs, DiscoveryRecordsValueAndCccdHandles)
{
    ProfileHrs p;
    auto v = declValue(0x0025, 0x2A37);
    EXPECT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));
    EXPECT_EQ(p.valueHandle(), 0x0025);
    EXPECT_EQ(p.cccdHandle(), 0x0026);  // value + 1
}

TEST(ProfileHrs, DiscoveryRejectsWrongUuidAndShortData)
{
    ProfileHrs p;
    auto other = declValue(0x0025, 0x2A38);  // not the measurement char
    EXPECT_FALSE(p.onCharByUuidResp(0x0024, other.data(),
            static_cast<uint8_t>(other.size())));
    EXPECT_EQ(p.valueHandle(), 0);

    uint8_t shortVal[4] = { 0x10, 0x25, 0x00, 0x37 };  // 128-bit form / truncated
    EXPECT_FALSE(p.onCharByUuidResp(0x0024, shortVal, sizeof(shortVal)));
    EXPECT_EQ(p.valueHandle(), 0);
}

TEST(ProfileHrs, ParsesUint8Bpm)
{
    ProfileHrs p;
    uint8_t notif[2] = { 0x00, 72 };  // flags=0 (uint8, no contact bits), 72 bpm
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.kind, AccessorySample::Kind::HeartRate);
    EXPECT_EQ(s.value, 72);
    EXPECT_EQ(s.trust, 3);  // contact not supported -> treated as good
}

TEST(ProfileHrs, ParsesUint16Bpm)
{
    ProfileHrs p;
    uint8_t notif[3] = { 0x01, 0x2C, 0x01 };  // flags bit0=1 -> uint16, 300 bpm
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.value, 300);
}

TEST(ProfileHrs, RejectsZeroBpmWarmup)
{
    ProfileHrs p;
    uint8_t notif[2] = { 0x00, 0 };
    AccessorySample s;
    EXPECT_FALSE(p.parseNotification(0, notif, sizeof(notif), s));
}

TEST(ProfileHrs, ContactSupportedButAbsentIsDegradedTrust)
{
    ProfileHrs p;
    // flags: contact supported (0x04) but not detected (0x02 clear).
    uint8_t notif[2] = { 0x04, 80 };
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.trust, 1);
}

TEST(ProfileHrs, ContactSupportedAndDetectedIsGoodTrust)
{
    ProfileHrs p;
    uint8_t notif[2] = { 0x06, 80 };  // supported (0x04) + detected (0x02)
    AccessorySample s;
    ASSERT_TRUE(p.parseNotification(0, notif, sizeof(notif), s));
    EXPECT_EQ(s.trust, 3);
}

TEST(ProfileHrs, RejectsTruncatedFrames)
{
    ProfileHrs p;
    AccessorySample s;
    uint8_t one[1] = { 0x00 };
    EXPECT_FALSE(p.parseNotification(0, one, sizeof(one), s));  // len < 2
    uint8_t u16short[2] = { 0x01, 0x2C };                       // 16-bit flag, len < 3
    EXPECT_FALSE(p.parseNotification(0, u16short, sizeof(u16short), s));
}

TEST(ProfileHrs, IgnoresNotificationsForOtherHandlesAfterDiscovery)
{
    ProfileHrs p;
    auto v = declValue(0x0025, 0x2A37);
    ASSERT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));

    uint8_t notif[2] = { 0x00, 72 };
    AccessorySample s;
    EXPECT_FALSE(p.parseNotification(0x0099, notif, sizeof(notif), s));  // wrong handle
    EXPECT_TRUE(p.parseNotification(0x0025, notif, sizeof(notif), s));   // our handle
}

TEST(ProfileHrs, ResetClearsHandles)
{
    ProfileHrs p;
    auto v = declValue(0x0025, 0x2A37);
    ASSERT_TRUE(p.onCharByUuidResp(0x0024, v.data(), static_cast<uint8_t>(v.size())));
    p.reset();
    EXPECT_EQ(p.valueHandle(), 0);
    EXPECT_EQ(p.cccdHandle(), 0);
}
