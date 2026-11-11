// Host unit tests for the Custom Android Notification Service (CANS) data
// parser.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Components/External/BLE/Services/CANS/ParserCANS.hpp"

using namespace Ble::CANS;

TEST(ParserCANS, ParseNotificationValid)
{
    // Protocol, ActionID, UID (4 bytes LE = 0x12345678), CategoryID.
    const uint8_t msg[] = {
        kProtocolVersion, ACTION_ID_NOTIFICATION_MODIFIED,
        0x78, 0x56, 0x34, 0x12,
        CATEGORY_ID_MESSAGE
    };

    Notification ns {};
    ASSERT_TRUE(ParseNotification(msg, sizeof(msg), ns));
    EXPECT_EQ(ns.protocol, kProtocolVersion);
    EXPECT_EQ(ns.action, ACTION_ID_NOTIFICATION_MODIFIED);
    EXPECT_EQ(ns.uid, 0x12345678u);
    EXPECT_EQ(ns.category, CATEGORY_ID_MESSAGE);
}

TEST(ParserCANS, ParseNotificationRejectsShortPacket)
{
    Notification ns {};
    EXPECT_FALSE(ParseNotification(nullptr, 7, ns));

    const uint8_t buf[6] = {};
    EXPECT_FALSE(ParseNotification(buf, sizeof(buf), ns));
}

TEST(ParserCANS, AttributeParserAssemblesAcrossChunks)
{
    AttrInfo attrs[] = {
        { ATTRIBUTE_ID_APP_ID, 0, {} },
        { ATTRIBUTE_ID_TITLE, 0, {} },
    };
    uint8_t work[128] {};

    AttributeParser parser;
    parser.init(0x12345678u, attrs, 2, work, sizeof(work));

    // CommandID + UID + [AppId, len=3, "com"] + [Title, len=2, "Hi"].
    const uint8_t resp[] = {
        COMMAND_ID_REQUEST_ATTRIBUTES,
        0x78, 0x56, 0x34, 0x12,
        ATTRIBUTE_ID_APP_ID, 0x03, 0x00, 'c', 'o', 'm',
        ATTRIBUTE_ID_TITLE, 0x02, 0x00, 'H', 'i'
    };

    // Split mid-attribute to exercise streaming reassembly.
    EXPECT_EQ(parser.appendData(resp, 9), AttributeParser::Status::OK);
    EXPECT_EQ(parser.appendData(resp + 9, sizeof(resp) - 9),
            AttributeParser::Status::DONE);

    EXPECT_EQ(attrs[0].strv, std::string_view("com"));
    EXPECT_EQ(attrs[1].strv, std::string_view("Hi"));
}

TEST(ParserCANS, AttributeParserAssemblesIdenticalConsecutiveChunks)
{
    // Repetitive message content ("hahaha...", "!!!", emoji runs) produces
    // byte-identical consecutive BLE chunks whenever the repeat period
    // divides the chunk size. Those are legitimate data: the transport layer
    // must not dedup them (a former response-path CRC filter did, losing the
    // notification deterministically), and the parser must assemble them.
    const uint16_t chunkSize = 20;
    std::string body;
    for (int i = 0; i < 6 * chunkSize / 2; i++) {
        body += "ha";
    }

    AttrInfo attrs[] = { { ATTRIBUTE_ID_MSG, 0, {} } };
    uint8_t work[512] {};

    AttributeParser parser;
    parser.init(0x12345678u, attrs, 1, work, sizeof(work));

    std::vector<uint8_t> resp = {
        COMMAND_ID_REQUEST_ATTRIBUTES, 0x78, 0x56, 0x34, 0x12,
        ATTRIBUTE_ID_MSG,
        static_cast<uint8_t>(body.size() & 0xFF),
        static_cast<uint8_t>(body.size() >> 8)
    };
    resp.insert(resp.end(), body.begin(), body.end());

    AttributeParser::Status status = AttributeParser::Status::ERROR;
    for (size_t off = 0; off < resp.size(); off += chunkSize) {
        uint16_t len = static_cast<uint16_t>(
                std::min<size_t>(chunkSize, resp.size() - off));
        status = parser.appendData(resp.data() + off, len);
        if (off + len < resp.size()) {
            ASSERT_EQ(status, AttributeParser::Status::OK)
                    << "offset " << off;
        }
    }

    EXPECT_EQ(status, AttributeParser::Status::DONE);
    EXPECT_EQ(attrs[0].strv, std::string_view(body));
}

TEST(ParserCANS, AttributeParserDiscardsStaleResponse)
{
    AttrInfo attrs[] = { { ATTRIBUTE_ID_APP_ID, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x11111111u, attrs, 1, work, sizeof(work));

    // A late response to a previous request (different UID) must be discarded
    // as stale without failing the in-flight fetch...
    const uint8_t stale[] = {
        COMMAND_ID_REQUEST_ATTRIBUTES, 0x78, 0x56, 0x34, 0x12,
        ATTRIBUTE_ID_APP_ID, 0x01, 0x00, 'x'
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
        COMMAND_ID_REQUEST_ATTRIBUTES, 0x11, 0x11, 0x11, 0x11,
        ATTRIBUTE_ID_APP_ID, 0x03, 0x00, 'c', 'o', 'm'
    };
    EXPECT_EQ(parser.appendData(expected, sizeof(expected)),
            AttributeParser::Status::DONE);
    EXPECT_EQ(attrs[0].strv, std::string_view("com"));
}

TEST(ParserCANS, AttributeParserRejectsDataAfterFinalAttribute)
{
    AttrInfo attrs[] = { { ATTRIBUTE_ID_MSG, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x12345678u, attrs, 1, work, sizeof(work));

    const uint8_t resp[] = {
        COMMAND_ID_REQUEST_ATTRIBUTES, 0x78, 0x56, 0x34, 0x12,
        ATTRIBUTE_ID_MSG, 0x04, 0x00, 'B', 'o', 'd', 'y'
    };
    ASSERT_EQ(parser.appendData(resp, sizeof(resp)),
            AttributeParser::Status::DONE);

    // An app-level duplicate of the response delivered before the service
    // thread disarms the parser (the traffic the old response-path CRC
    // filter used to eat) must be rejected — the attribute list is already
    // fully consumed, and parsing on would index past its end.
    EXPECT_EQ(parser.appendData(resp, sizeof(resp)),
            AttributeParser::Status::ERROR);
    EXPECT_EQ(attrs[0].strv, std::string_view("Body"));
}

TEST(ParserCANS, AttributeParserFailsOnErrorResponse)
{
    AttrInfo attrs[] = { { ATTRIBUTE_ID_APP_ID, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x11111111u, attrs, 1, work, sizeof(work));

    // A phone-side error response is a single-byte packet: a real failure,
    // not a stale leftover, and must fail the fetch immediately.
    const uint8_t err[] = { ERROR_CODE_UID_NOT_FOUND };
    EXPECT_EQ(parser.appendData(err, sizeof(err)),
            AttributeParser::Status::ERROR);
}

TEST(ParserCANS, AttributeParserDiscardsStaleUtf8ChunkResemblingError)
{
    AttrInfo attrs[] = { { ATTRIBUTE_ID_APP_ID, 0, {} } };
    uint8_t work[64] {};

    AttributeParser parser;
    parser.init(0x11111111u, attrs, 1, work, sizeof(work));

    // A multi-byte chunk of a discarded stale body can start with 0x80..0x82
    // (UTF-8 continuation bytes in Cyrillic/emoji text). That is stale data to
    // drop, not a phone error response (those are single-byte packets) — it
    // must not fail the in-flight fetch.
    const uint8_t staleText[] = { 0x81, 0x80, 'a', 'b', 0x82, 'c' };
    EXPECT_EQ(parser.appendData(staleText, sizeof(staleText)),
            AttributeParser::Status::OK);

    // The expected response still completes normally afterwards.
    const uint8_t expected[] = {
        COMMAND_ID_REQUEST_ATTRIBUTES, 0x11, 0x11, 0x11, 0x11,
        ATTRIBUTE_ID_APP_ID, 0x03, 0x00, 'c', 'o', 'm'
    };
    EXPECT_EQ(parser.appendData(expected, sizeof(expected)),
            AttributeParser::Status::DONE);
    EXPECT_EQ(attrs[0].strv, std::string_view("com"));
}
