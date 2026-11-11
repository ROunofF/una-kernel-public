// Host unit tests for the Custom Android Media Service (CAMS) data parser.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Components/External/BLE/Services/CAMS/ParserCAMS.hpp"

using namespace Ble::CAMS;

namespace
{

// Convenience: prepend the 2-byte Now Playing header to an attribute list.
std::vector<uint8_t> packet(std::vector<uint8_t> attrs)
{
    std::vector<uint8_t> p { kCmdNowPlaying, 0x00 };
    p.insert(p.end(), attrs.begin(), attrs.end());
    return p;
}

// Parse a whole buffer, taking its size as the parser's uint16_t length.
bool parse(const std::vector<uint8_t>& p, MediaUpdate& out)
{
    return ParseNowPlayingUpdate(p.data(), static_cast<uint16_t>(p.size()), out);
}

} // namespace

TEST(ParserCAMS, ParseStringsAndNumerics)
{
    // Title "Song", Duration 240 s, State Playing, Elapsed 0.
    auto p = packet({
        MediaAttributeIDTitle, 0x04, 'S', 'o', 'n', 'g',
        MediaAttributeIDDuration, 0x04, 0xF0, 0x00, 0x00, 0x00,
        MediaAttributeIDState, 0x01, PlaybackStatePlaying,
        MediaAttributeIDElapsed, 0x04, 0x00, 0x00, 0x00, 0x00,
    });

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    ASSERT_TRUE(u.title.has_value());
    EXPECT_EQ(*u.title, std::string_view("Song"));
    ASSERT_TRUE(u.durationSec.has_value());
    EXPECT_EQ(*u.durationSec, 240u);
    ASSERT_TRUE(u.state.has_value());
    EXPECT_EQ(*u.state, PlaybackStatePlaying);
    ASSERT_TRUE(u.elapsedSec.has_value());
    EXPECT_EQ(*u.elapsedSec, 0u);

    // Absent fields stay nullopt.
    EXPECT_FALSE(u.artist.has_value());
    EXPECT_FALSE(u.volumePct.has_value());
}

TEST(ParserCAMS, ParseAllNumericFields)
{
    auto p = packet({
        MediaAttributeIDRate, 0x02, 0x64, 0x00,        // 100 -> 1.00x
        MediaAttributeIDVolume, 0x01, 0x4B,            // 75 %
        MediaAttributeIDQueueIndex, 0x02, 0x03, 0x00,  // 3
        MediaAttributeIDQueueCount, 0x02, 0x0A, 0x00,  // 10
        MediaAttributeIDShuffleMode, 0x01, 0x01,
        MediaAttributeIDRepeatMode, 0x01, 0x02,
    });

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    EXPECT_EQ(u.rateCenti.value_or(0), 100u);
    EXPECT_EQ(u.volumePct.value_or(0), 75u);
    EXPECT_EQ(u.queueIndex.value_or(0), 3u);
    EXPECT_EQ(u.queueCount.value_or(0), 10u);
    EXPECT_EQ(u.shuffleMode.value_or(0), 1u);
    EXPECT_EQ(u.repeatMode.value_or(0), 2u);
}

TEST(ParserCAMS, EmptyAttributeListIsValid)
{
    auto p = packet({});

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    EXPECT_FALSE(u.title.has_value());
}

TEST(ParserCAMS, EmptyStringValue)
{
    auto p = packet({ MediaAttributeIDTitle, 0x00 });

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    ASSERT_TRUE(u.title.has_value());
    EXPECT_EQ(u.title->size(), 0u);
}

TEST(ParserCAMS, UnknownAttributeIsSkipped)
{
    // Unknown 0x7F (len 2) sits between two known attributes and must be skipped.
    auto p = packet({
        0x7F, 0x02, 0xAA, 0xBB,
        MediaAttributeIDState, 0x01, PlaybackStatePaused,
    });

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    ASSERT_TRUE(u.state.has_value());
    EXPECT_EQ(*u.state, PlaybackStatePaused);
}

TEST(ParserCAMS, NumericWrongLengthIsSkipped)
{
    // Duration declared as 2 bytes (should be 4): ignored, parsing continues.
    auto p = packet({
        MediaAttributeIDDuration, 0x02, 0x10, 0x00,
        MediaAttributeIDState, 0x01, PlaybackStatePlaying,
    });

    MediaUpdate u {};
    ASSERT_TRUE(parse(p, u));
    EXPECT_FALSE(u.durationSec.has_value());
    EXPECT_EQ(u.state.value_or(0xFF), PlaybackStatePlaying);
}

TEST(ParserCAMS, MalformedTlvRunsPastBuffer)
{
    // Title claims 4 bytes but only 2 follow.
    auto p = packet({ MediaAttributeIDTitle, 0x04, 'A', 'B' });

    MediaUpdate u {};
    EXPECT_FALSE(parse(p, u));
}

TEST(ParserCAMS, DanglingAttributeHeader)
{
    // An attribute id with no length byte.
    auto p = packet({ MediaAttributeIDState });

    MediaUpdate u {};
    EXPECT_FALSE(parse(p, u));
}

TEST(ParserCAMS, RejectsWrongCommandAndShortPacket)
{
    const uint8_t wrongCmd[] = { 0x10, 0x00, MediaAttributeIDState, 0x01, 0x01 };
    MediaUpdate u {};
    EXPECT_FALSE(ParseNowPlayingUpdate(wrongCmd, static_cast<uint16_t>(sizeof(wrongCmd)), u));

    const uint8_t shortPkt[] = { kCmdNowPlaying };
    EXPECT_FALSE(ParseNowPlayingUpdate(shortPkt, 1, u));
    EXPECT_FALSE(ParseNowPlayingUpdate(nullptr, 0, u));
}

TEST(ParserCAMS, BuildRemoteCommandEventPayload)
{
    uint8_t buff[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

    EXPECT_EQ(BuildRemoteCommandEvent(MediaCommandIDNextTrack, buff, sizeof(buff)),
            kRemoteCommandEventLen);
    EXPECT_EQ(buff[0], kEvtRemoteCommand);
    EXPECT_EQ(buff[1], MediaCommandIDNextTrack);
}

TEST(ParserCAMS, BuildRemoteCommandEventRejectsSmallBuffer)
{
    uint8_t buff[2];
    EXPECT_EQ(BuildRemoteCommandEvent(MediaCommandIDPlay, buff, 1), 0u);
    EXPECT_EQ(BuildRemoteCommandEvent(MediaCommandIDPlay, nullptr, 2), 0u);
}

TEST(ParserCAMS, Validators)
{
    EXPECT_TRUE(IsMediaCommandID(MediaCommandIDPlay));
    EXPECT_TRUE(IsMediaCommandID(MediaCommandIDVolumeDown));
    EXPECT_FALSE(IsMediaCommandID(MediaCommandIDMax));
    EXPECT_FALSE(IsMediaCommandID(0xFF));
}
