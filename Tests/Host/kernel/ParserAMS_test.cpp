// Host unit tests for the Apple Media Service (AMS) data parser.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Components/External/BLE/Services/AMS/ParserAMS.hpp"

using namespace Ble::AMS;

TEST(ParserAMS, ParseEntityUpdateValid)
{
    const uint8_t msg[] = {
        EntityIDTrack, TrackAttributeIDTitle, 0x00, 'A', 'B', 'C'
    };

    EntityUpdate eu {};
    ASSERT_TRUE(ParseEntityUpdate(msg, sizeof(msg), eu));
    EXPECT_EQ(eu.entityID, EntityIDTrack);
    EXPECT_EQ(eu.attributeID, TrackAttributeIDTitle);
    EXPECT_FALSE(eu.truncated);
    EXPECT_EQ(eu.value, std::string_view("ABC"));
}

TEST(ParserAMS, ParseEntityUpdateTruncatedFlag)
{
    const uint8_t msg[] = {
        EntityIDTrack, TrackAttributeIDTitle, EntityUpdateFlagTruncated, 'X'
    };

    EntityUpdate eu {};
    ASSERT_TRUE(ParseEntityUpdate(msg, sizeof(msg), eu));
    EXPECT_TRUE(eu.truncated);
    EXPECT_EQ(eu.value, std::string_view("X"));
}

TEST(ParserAMS, ParseEntityUpdateEmptyValue)
{
    const uint8_t msg[] = { EntityIDPlayer, PlayerAttributeIDVolume, 0x00 };

    EntityUpdate eu {};
    ASSERT_TRUE(ParseEntityUpdate(msg, sizeof(msg), eu));
    EXPECT_EQ(eu.value.size(), 0u);
}

TEST(ParserAMS, ParseEntityUpdateTooShort)
{
    const uint8_t msg[] = { EntityIDTrack, TrackAttributeIDTitle };

    EntityUpdate eu {};
    EXPECT_FALSE(ParseEntityUpdate(msg, sizeof(msg), eu));
    EXPECT_FALSE(ParseEntityUpdate(nullptr, 0, eu));
}

TEST(ParserAMS, BuildRemoteCommandSingleByte)
{
    uint8_t buff[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

    EXPECT_EQ(BuildRemoteCommand(RemoteCommandIDPause, buff, sizeof(buff)), 1u);
    EXPECT_EQ(buff[0], RemoteCommandIDPause);
}

TEST(ParserAMS, BuildRemoteCommandRejectsEmptyBuffer)
{
    uint8_t buff[1];
    EXPECT_EQ(BuildRemoteCommand(RemoteCommandIDPlay, buff, 0), 0u);
    EXPECT_EQ(BuildRemoteCommand(RemoteCommandIDPlay, nullptr, 1), 0u);
}

TEST(ParserAMS, BuildEntityUpdateRegistrationPacket)
{
    const uint8_t attrs[] = {
        TrackAttributeIDArtist, TrackAttributeIDAlbum,
        TrackAttributeIDTitle, TrackAttributeIDDuration
    };
    uint8_t buff[8] {};

    uint16_t n = BuildEntityUpdateRegistration(EntityIDTrack, attrs,
            sizeof(attrs), buff, sizeof(buff));

    ASSERT_EQ(n, 5u);
    EXPECT_EQ(buff[0], EntityIDTrack);
    EXPECT_EQ(buff[1], TrackAttributeIDArtist);
    EXPECT_EQ(buff[2], TrackAttributeIDAlbum);
    EXPECT_EQ(buff[3], TrackAttributeIDTitle);
    EXPECT_EQ(buff[4], TrackAttributeIDDuration);
}

TEST(ParserAMS, BuildEntityUpdateRegistrationRejectsOverflowAndEmpty)
{
    const uint8_t attrs[] = { 0, 1, 2, 3 };
    uint8_t small[4] {};

    // Needs 1 + 4 = 5 bytes, buffer holds 4.
    EXPECT_EQ(BuildEntityUpdateRegistration(EntityIDTrack, attrs, 4, small,
            sizeof(small)), 0u);
    EXPECT_EQ(BuildEntityUpdateRegistration(EntityIDTrack, attrs, 0, small,
            sizeof(small)), 0u);
    EXPECT_EQ(BuildEntityUpdateRegistration(EntityIDTrack, nullptr, 4, small,
            sizeof(small)), 0u);
}

TEST(ParserAMS, ParsePlaybackInfoFull)
{
    uint8_t state = 0xFF;
    float rate = -1.0f;
    float elapsed = -1.0f;

    ASSERT_TRUE(ParsePlaybackInfo("1,1.0,76.783", state, rate, elapsed));
    EXPECT_EQ(state, PlaybackStatePlaying);
    EXPECT_FLOAT_EQ(rate, 1.0f);
    EXPECT_FLOAT_EQ(elapsed, 76.783f);
}

TEST(ParserAMS, ParsePlaybackInfoPausedEmptyFields)
{
    uint8_t state = 0xFF;
    float rate = -1.0f;
    float elapsed = -1.0f;

    // Paused player reports "0,," - rate and elapsed are absent.
    ASSERT_TRUE(ParsePlaybackInfo("0,,", state, rate, elapsed));
    EXPECT_EQ(state, PlaybackStatePaused);
    EXPECT_FLOAT_EQ(rate, 0.0f);
    EXPECT_FLOAT_EQ(elapsed, 0.0f);
}

TEST(ParserAMS, ParsePlaybackInfoStateOnlyAndEmpty)
{
    uint8_t state = 0xFF;
    float rate = 9.0f;
    float elapsed = 9.0f;

    // State only, no commas.
    ASSERT_TRUE(ParsePlaybackInfo("2", state, rate, elapsed));
    EXPECT_EQ(state, PlaybackStateRewinding);
    EXPECT_FLOAT_EQ(rate, 0.0f);
    EXPECT_FLOAT_EQ(elapsed, 0.0f);

    // Empty value -> false, all defaulted.
    state = 0xFF;
    EXPECT_FALSE(ParsePlaybackInfo("", state, rate, elapsed));
    EXPECT_EQ(state, PlaybackStatePaused);
}

TEST(ParserAMS, ParsePlaybackInfoPresenceFlags)
{
    uint8_t state = 0;
    float rate = 0.0f;
    float elapsed = 0.0f;
    bool ratePresent = true;
    bool elapsedPresent = true;

    // Paused "0,," -> rate and elapsed absent: callers must keep their last value.
    ASSERT_TRUE(ParsePlaybackInfo("0,,", state, rate, elapsed,
            &ratePresent, &elapsedPresent));
    EXPECT_FALSE(ratePresent);
    EXPECT_FALSE(elapsedPresent);

    // Playing with both fields -> both present.
    ratePresent = false;
    elapsedPresent = false;
    ASSERT_TRUE(ParsePlaybackInfo("1,1.0,76.783", state, rate, elapsed,
            &ratePresent, &elapsedPresent));
    EXPECT_TRUE(ratePresent);
    EXPECT_TRUE(elapsedPresent);
}

TEST(ParserAMS, Validators)
{
    EXPECT_TRUE(IsRemoteCommandID(RemoteCommandIDPlay));
    EXPECT_TRUE(IsRemoteCommandID(RemoteCommandIDBookmarkTrack));
    EXPECT_FALSE(IsRemoteCommandID(RemoteCommandIDMax));
    EXPECT_FALSE(IsRemoteCommandID(0xFF));

    EXPECT_TRUE(IsEntityID(EntityIDPlayer));
    EXPECT_TRUE(IsEntityID(EntityIDTrack));
    EXPECT_FALSE(IsEntityID(EntityIDMax));
    EXPECT_FALSE(IsEntityID(0xFF));
}
