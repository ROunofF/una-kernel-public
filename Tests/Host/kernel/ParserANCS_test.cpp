// Host unit tests for the Apple Notification Center Service (ANCS) data parser.

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "Components/External/BLE/Services/ANCS/ParserANCS.hpp"

using namespace Ble::ANCS;

TEST(ParserANCS, ParseNotificationSourceValid)
{
    // EventID, Flags, CategoryID, CategoryCount, UID (4 bytes LE = 0x0A0B0C0D).
    const uint8_t msg[] = {
        EventIDNotificationAdded, EventFlagImportant, CategoryIDSocial, 2,
        0x0D, 0x0C, 0x0B, 0x0A
    };

    NotificationSource ns {};
    ASSERT_TRUE(ParseNotificationSource(msg, sizeof(msg), ns));
    EXPECT_EQ(ns.EventID, EventIDNotificationAdded);
    EXPECT_EQ(ns.EventFlags, EventFlagImportant);
    EXPECT_EQ(ns.CategoryID, CategoryIDSocial);
    EXPECT_EQ(ns.CategoryCount, 2);
    EXPECT_EQ(ns.NotificationUID, 0x0A0B0C0Du);
}

TEST(ParserANCS, ParseNotificationSourceTooShort)
{
    NotificationSource ns {};
    EXPECT_FALSE(ParseNotificationSource(nullptr, 0, ns));

    // A full packet is 8 bytes (the 4-byte UID spans message[4]..message[7]).
    // Any shorter peer-supplied length must be rejected before those reads, so
    // a 5-7 byte packet does not read past its buffer. The backing buffer is
    // sized exactly to len so a regression to the old len<5 guard reads OOB.
    for (uint16_t len = 1; len < 8; len++) {
        const uint8_t buf[7] = { 0, 0, 0, 0, 0, 0, 0 };
        EXPECT_FALSE(ParseNotificationSource(buf, len, ns))
                << "len=" << len << " should be rejected";
    }
}

TEST(ParserANCS, CreateAttributeRequestPacket)
{
    AttrInfo attrs[] = {
        { NotificationAttributeIDAppIdentifier, 0, {} },
        { NotificationAttributeIDTitle, 32, {} },  // needs a 2-byte max length
    };
    uint8_t buff[16] {};

    uint16_t n = CreateAttributeRequest(0x12345678u, attrs, 2, buff,
            sizeof(buff));

    // CommandID(1) + UID(4) + AppId(1) + Title(1) + maxLen(2) = 9.
    ASSERT_EQ(n, 9u);
    EXPECT_EQ(buff[0], CommandIDGetNotificationAttributes);
    EXPECT_EQ(buff[1], 0x78);
    EXPECT_EQ(buff[2], 0x56);
    EXPECT_EQ(buff[3], 0x34);
    EXPECT_EQ(buff[4], 0x12);
    EXPECT_EQ(buff[5], NotificationAttributeIDAppIdentifier);
    EXPECT_EQ(buff[6], NotificationAttributeIDTitle);
    EXPECT_EQ(buff[7], 32);  // maxLen low byte
    EXPECT_EQ(buff[8], 0);   // maxLen high byte
}

TEST(ParserANCS, CreateAttributeRequestRejectsInvalidInput)
{
    AttrInfo bad[] = { { 0xFF, 0, {} } };       // invalid attribute ID
    uint8_t buff[16] {};
    EXPECT_EQ(CreateAttributeRequest(1, bad, 1, buff, sizeof(buff)), 0u);

    AttrInfo ok[] = { { NotificationAttributeIDAppIdentifier, 0, {} } };
    EXPECT_EQ(CreateAttributeRequest(1, ok, 1, buff, 4), 0u);   // buffer too small
    EXPECT_EQ(CreateAttributeRequest(1, nullptr, 1, buff, sizeof(buff)), 0u);
}

TEST(ParserANCS, CreateActionRequestPacket)
{
    uint8_t buff[8] {};
    uint16_t n = CreateActionRequest(0x12345678u, ActionIDPositive, buff,
            sizeof(buff));

    ASSERT_EQ(n, 6u);
    EXPECT_EQ(buff[0], CommandIDPerformNotificationAction);
    EXPECT_EQ(buff[1], 0x78);
    EXPECT_EQ(buff[2], 0x56);
    EXPECT_EQ(buff[3], 0x34);
    EXPECT_EQ(buff[4], 0x12);
    EXPECT_EQ(buff[5], ActionIDPositive);

    EXPECT_EQ(CreateActionRequest(1, ActionIDPositive, buff, 5), 0u);  // too small
}

TEST(ParserANCS, AttributeParserAssemblesAcrossChunks)
{
    const uint32_t uid = 0x12345678u;
    AttrInfo attrs[] = {
        { NotificationAttributeIDAppIdentifier, 0, {} },
        { NotificationAttributeIDTitle, 0, {} },
    };
    uint8_t work[128] {};

    AttributeParser parser;
    parser.init(uid, attrs, 2, work, sizeof(work));

    // CommandID + UID + [AppId, len=3, "com"] + [Title, len=2, "Hi"].
    const uint8_t resp[] = {
        CommandIDGetNotificationAttributes,
        0x78, 0x56, 0x34, 0x12,
        NotificationAttributeIDAppIdentifier, 0x03, 0x00, 'c', 'o', 'm',
        NotificationAttributeIDTitle, 0x02, 0x00, 'H', 'i'
    };

    // Split mid-attribute to exercise streaming reassembly.
    EXPECT_EQ(parser.appendData(resp, 9), AttributeParser::Status::OK);
    EXPECT_EQ(parser.appendData(resp + 9, sizeof(resp) - 9),
            AttributeParser::Status::DONE);

    EXPECT_EQ(attrs[0].strv, std::string_view("com"));
    EXPECT_EQ(attrs[1].strv, std::string_view("Hi"));
}

TEST(ParserANCS, AttributeParserDiscardsStaleResponse)
{
    AttrInfo attrs[] = { { NotificationAttributeIDAppIdentifier, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x11111111u, attrs, 1, work, sizeof(work));

    // A late response to a previous request (different UID) must be discarded
    // as stale without failing the in-flight fetch...
    const uint8_t stale[] = {
        CommandIDGetNotificationAttributes, 0x78, 0x56, 0x34, 0x12,
        NotificationAttributeIDAppIdentifier, 0x01, 0x00, 'x'
    };
    EXPECT_EQ(parser.appendData(stale, sizeof(stale)),
            AttributeParser::Status::OK);

    // ...a continuation chunk of the discarded response is dropped the same
    // way...
    const uint8_t staleTail[] = { 'y', 'z' };
    EXPECT_EQ(parser.appendData(staleTail, sizeof(staleTail)),
            AttributeParser::Status::OK);

    // ...and the expected response still completes normally afterwards.
    const uint8_t expected[] = {
        CommandIDGetNotificationAttributes, 0x11, 0x11, 0x11, 0x11,
        NotificationAttributeIDAppIdentifier, 0x03, 0x00, 'c', 'o', 'm'
    };
    EXPECT_EQ(parser.appendData(expected, sizeof(expected)),
            AttributeParser::Status::DONE);
    EXPECT_EQ(attrs[0].strv, std::string_view("com"));
}

TEST(ParserANCS, AttributeParserRejectsDataAfterFinalAttribute)
{
    AttrInfo attrs[] = { { NotificationAttributeIDMessage, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x12345678u, attrs, 1, work, sizeof(work));

    const uint8_t resp[] = {
        CommandIDGetNotificationAttributes, 0x78, 0x56, 0x34, 0x12,
        NotificationAttributeIDMessage, 0x04, 0x00, 'B', 'o', 'd', 'y'
    };
    ASSERT_EQ(parser.appendData(resp, sizeof(resp)),
            AttributeParser::Status::DONE);

    // A duplicated response delivered before the fetch disarms the parser
    // must be rejected — the attribute list is already fully consumed, and
    // parsing on would index past its end.
    EXPECT_EQ(parser.appendData(resp, sizeof(resp)),
            AttributeParser::Status::ERROR);
    EXPECT_EQ(attrs[0].strv, std::string_view("Body"));
}

TEST(ParserANCS, ConvertDateToTmParsesIso)
{
    std::tm tm = ConvertDateToTm("20260622T134500", 15);
    EXPECT_EQ(tm.tm_year, 2026 - 1900);
    EXPECT_EQ(tm.tm_mon, 6 - 1);
    EXPECT_EQ(tm.tm_mday, 22);
    EXPECT_EQ(tm.tm_hour, 13);
    EXPECT_EQ(tm.tm_min, 45);
    EXPECT_EQ(tm.tm_sec, 0);
}
