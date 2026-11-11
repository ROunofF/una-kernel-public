/**
 * Host unit tests for the accessory advertising-data parsers.
 *
 * These functions parse untrusted, length-prefixed data straight off the air and
 * previously shipped with NO test coverage as anonymous-namespace helpers inside
 * AccessoryCentral.cpp. The malformed-input cases below are therefore the point
 * of this file, not an afterthought: a length byte running past the end of the
 * buffer is the obvious way to walk a parser off its own array.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/AccessoryAdvParser.hpp"

namespace {

using Ble::advertisesAnyService;
using Ble::advertisesService;
using Ble::parseAdvName;

constexpr uint16_t kHrs = 0x180D;
constexpr uint16_t kCsc = 0x1816;
constexpr uint16_t kCps = 0x1818;

// Append one AD structure: [len][type][payload...], where len counts the type.
void pushAd(std::vector<uint8_t> &v, uint8_t type,
        const std::vector<uint8_t> &payload)
{
    v.push_back(static_cast<uint8_t>(payload.size() + 1));
    v.push_back(type);
    v.insert(v.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> uuid16(uint16_t u)
{
    return { static_cast<uint8_t>(u & 0xFF), static_cast<uint8_t>(u >> 8) };
}

std::vector<uint8_t> nameBytes(const char *s)
{
    return std::vector<uint8_t>(s, s + std::strlen(s));
}

} // namespace

TEST(AccessoryAdvParser, FindsUuidInCompleteList)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kCsc));   // complete list of 16-bit UUIDs
    EXPECT_TRUE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCsc));
}

TEST(AccessoryAdvParser, FindsUuidInIncompleteList)
{
    // AD type 0x02 is just as valid as 0x03 and must not be ignored.
    std::vector<uint8_t> ad;
    pushAd(ad, 0x02, uuid16(kHrs));
    EXPECT_TRUE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kHrs));
}

TEST(AccessoryAdvParser, RejectsUuidNotPresent)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kHrs));
    EXPECT_FALSE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCsc));
}

TEST(AccessoryAdvParser, FindsUuidAmongSeveralInOneList)
{
    // A trainer may list CPS, CSC and FTMS together in a single AD structure.
    std::vector<uint8_t> entries;
    for (uint16_t u : { uint16_t(0x1826), kCps, kCsc }) {
        auto b = uuid16(u);
        entries.insert(entries.end(), b.begin(), b.end());
    }
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, entries);

    EXPECT_TRUE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCsc));
    EXPECT_TRUE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCps));
    EXPECT_FALSE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kHrs));
}

TEST(AccessoryAdvParser, ScansPastEarlierAdStructures)
{
    // Flags and a name before the UUID list: the walk must step over both.
    std::vector<uint8_t> ad;
    pushAd(ad, 0x01, { 0x06 });                 // flags
    pushAd(ad, 0x09, nameBytes("SPD-BLE0000001"));
    pushAd(ad, 0x03, uuid16(kCsc));
    EXPECT_TRUE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCsc));
}

TEST(AccessoryAdvParser, ReportsWhichUuidMatched)
{
    // The multi-profile case: one scan looking for HR, power and speed/cadence
    // at once has to know WHICH kind it just found, not merely that it found one.
    const uint16_t wanted[] = { kHrs, kCps, kCsc };
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kCps));

    uint16_t matched = 0;
    ASSERT_TRUE(advertisesAnyService(ad.data(), static_cast<uint8_t>(ad.size()),
            wanted, 3, &matched));
    EXPECT_EQ(matched, kCps);
}

TEST(AccessoryAdvParser, LeavesMatchedUntouchedOnMiss)
{
    const uint16_t wanted[] = { kHrs, kCps };
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kCsc));

    uint16_t matched = 0xBEEF;
    EXPECT_FALSE(advertisesAnyService(ad.data(), static_cast<uint8_t>(ad.size()),
            wanted, 2, &matched));
    EXPECT_EQ(matched, 0xBEEF);   // caller's value preserved
}

TEST(AccessoryAdvParser, EmptyWantedSetMatchesNothing)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kCsc));
    EXPECT_FALSE(advertisesAnyService(ad.data(), static_cast<uint8_t>(ad.size()),
            nullptr, 0));
    const uint16_t none[] = { kCsc };
    EXPECT_FALSE(advertisesAnyService(ad.data(), static_cast<uint8_t>(ad.size()),
            none, 0));
}

TEST(AccessoryAdvParser, HandlesMalformedLengths)
{
    // A length running past the end of the buffer is the classic way to walk a
    // parser off its array. Must terminate, not read out of bounds.
    uint8_t overrun[4] = { 0x40, 0x03, 0x16, 0x18 };  // claims 64 bytes
    EXPECT_FALSE(advertisesService(overrun, sizeof(overrun), kCsc));

    // Zero length terminates the list (trailing padding is zero-filled).
    uint8_t zeroLen[4] = { 0x00, 0x03, 0x16, 0x18 };
    EXPECT_FALSE(advertisesService(zeroLen, sizeof(zeroLen), kCsc));

    // Truncated UUID: an odd number of payload bytes must not read a half UUID.
    uint8_t oddPayload[3] = { 0x02, 0x03, 0x16 };
    EXPECT_FALSE(advertisesService(oddPayload, sizeof(oddPayload), kCsc));
}

TEST(AccessoryAdvParser, HandlesEmptyAndNullInput)
{
    EXPECT_FALSE(advertisesService(nullptr, 0, kCsc));
    uint8_t one[1] = { 0x00 };
    EXPECT_FALSE(advertisesService(one, 0, kCsc));
    EXPECT_FALSE(advertisesService(one, 1, kCsc));   // length byte only
}

TEST(AccessoryAdvParser, StopsAtMalformedStructureEvenIfLaterDataWouldMatch)
{
    // Once the walk hits a bad length it must stop, not resynchronise and go on
    // scanning -- otherwise a crafted payload steers the parse.
    std::vector<uint8_t> ad;
    ad.push_back(0x40);            // bogus length
    ad.push_back(0x03);
    auto good = uuid16(kCsc);
    ad.insert(ad.end(), good.begin(), good.end());
    pushAd(ad, 0x03, uuid16(kCsc));   // valid, but unreachable

    EXPECT_FALSE(advertisesService(ad.data(), static_cast<uint8_t>(ad.size()), kCsc));
}

TEST(AccessoryAdvParser, ParsesCompleteName)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x09, nameBytes("SPD-BLE0000001"));
    char out[24] = {};
    parseAdvName(ad.data(), static_cast<uint8_t>(ad.size()), out, sizeof(out));
    EXPECT_STREQ(out, "SPD-BLE0000001");
}

TEST(AccessoryAdvParser, ParsesShortenedName)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x08, nameBytes("CAD-BLE"));
    char out[24] = {};
    parseAdvName(ad.data(), static_cast<uint8_t>(ad.size()), out, sizeof(out));
    EXPECT_STREQ(out, "CAD-BLE");
}

TEST(AccessoryAdvParser, EmptiesOutputWhenNoNameAdvertised)
{
    std::vector<uint8_t> ad;
    pushAd(ad, 0x03, uuid16(kCsc));
    char out[24];
    std::memset(out, 'x', sizeof(out));
    parseAdvName(ad.data(), static_cast<uint8_t>(ad.size()), out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(AccessoryAdvParser, TruncatesOverlongNameWithoutOverflow)
{
    // An AD structure can carry more name than the 24-byte buffers that display
    // it, so truncation must be safe and NUL-terminated.
    std::vector<uint8_t> ad;
    pushAd(ad, 0x09, nameBytes("0123456789ABCDEFGHIJKLMNOPQRS"));

    char guarded[16 + 4];
    std::memset(guarded, '\xAB', sizeof(guarded));
    char *out = guarded;
    parseAdvName(ad.data(), static_cast<uint8_t>(ad.size()), out, 16);

    EXPECT_EQ(std::strlen(out), 15u);
    EXPECT_STREQ(out, "0123456789ABCDE");
    // The bytes beyond the declared size must be untouched.
    for (size_t i = 16; i < sizeof(guarded); ++i) {
        EXPECT_EQ(static_cast<unsigned char>(guarded[i]), 0xABu) << "at " << i;
    }
}

TEST(AccessoryAdvParser, NameHandlesMalformedAndNullInput)
{
    char out[24];
    std::memset(out, 'x', sizeof(out));
    parseAdvName(nullptr, 10, out, sizeof(out));
    EXPECT_STREQ(out, "");

    uint8_t overrun[4] = { 0x40, 0x09, 'A', 'B' };
    std::memset(out, 'x', sizeof(out));
    parseAdvName(overrun, sizeof(overrun), out, sizeof(out));
    EXPECT_STREQ(out, "");

    // outSize of 0 must not write anything at all.
    char none[1] = { 'Z' };
    parseAdvName(overrun, sizeof(overrun), none, 0);
    EXPECT_EQ(none[0], 'Z');
}
