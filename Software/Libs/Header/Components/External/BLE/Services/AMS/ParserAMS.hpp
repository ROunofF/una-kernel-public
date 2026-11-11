/**
 ******************************************************************************
 * @file    ParserAMS.hpp
 * @brief   Data parser for the Apple Media Service (AMS).
 ******************************************************************************
 *
 * Pure data helpers: parse an Entity Update notification and build the Remote
 * Command and Entity Update registration writes. No controller-stack, OS or
 * logging dependency, so this unit is host-testable.
 *
 ******************************************************************************
 */

#ifndef BLE_AMS_PARSER_AMS_HPP
#define BLE_AMS_PARSER_AMS_HPP

#include <cstdint>
#include <string_view>

namespace Ble
{

namespace AMS
{

// RemoteCommandID values (written to the Remote Command characteristic).
enum RemoteCommandID {
    RemoteCommandIDPlay                = 0,
    RemoteCommandIDPause               = 1,
    RemoteCommandIDTogglePlayPause     = 2,
    RemoteCommandIDNextTrack           = 3,
    RemoteCommandIDPreviousTrack       = 4,
    RemoteCommandIDVolumeUp            = 5,
    RemoteCommandIDVolumeDown          = 6,
    RemoteCommandIDAdvanceRepeatMode   = 7,
    RemoteCommandIDAdvanceShuffleMode  = 8,
    RemoteCommandIDSkipForward         = 9,
    RemoteCommandIDSkipBackward        = 10,
    RemoteCommandIDLikeTrack           = 11,
    RemoteCommandIDDislikeTrack        = 12,
    RemoteCommandIDBookmarkTrack       = 13,

    RemoteCommandIDMax
};

// EntityID values.
enum EntityID {
    EntityIDPlayer = 0,
    EntityIDQueue  = 1,
    EntityIDTrack  = 2,

    EntityIDMax
};

// Player entity attribute IDs.
enum PlayerAttributeID {
    PlayerAttributeIDName         = 0,
    PlayerAttributeIDPlaybackInfo = 1,  // "State,Rate,ElapsedTime" CSV string
    PlayerAttributeIDVolume       = 2,  // float string in [0.0, 1.0]
};

// Queue entity attribute IDs.
enum QueueAttributeID {
    QueueAttributeIDIndex       = 0,
    QueueAttributeIDCount       = 1,
    QueueAttributeIDShuffleMode = 2,
    QueueAttributeIDRepeatMode  = 3,
};

// Track entity attribute IDs.
enum TrackAttributeID {
    TrackAttributeIDArtist   = 0,
    TrackAttributeIDAlbum    = 1,
    TrackAttributeIDTitle    = 2,
    TrackAttributeIDDuration = 3,  // float string, seconds
};

// PlaybackState, the first field of PlayerAttributeIDPlaybackInfo.
enum PlaybackState {
    PlaybackStatePaused        = 0,
    PlaybackStatePlaying       = 1,
    PlaybackStateRewinding     = 2,
    PlaybackStateFastForwarding = 3,
};

// EntityUpdateFlags bitmask (third byte of an Entity Update notification).
enum EntityUpdateFlags {
    EntityUpdateFlagTruncated = (1 << 0),
};

// Error codes returned by writes to AMS characteristics.
enum ErrorCode {
    ErrorCodeInvalidState          = 0xA0,
    ErrorCodeInvalidCommand        = 0xA1,
    ErrorCodeInvalidParameterLength = 0xA2,
};

/**
 * @brief   Checks whether an ID is a valid RemoteCommandID.
 */
constexpr bool IsRemoteCommandID(uint8_t id)
{
    return id < RemoteCommandIDMax;
}

/**
 * @brief   Checks whether an ID is a valid EntityID.
 */
constexpr bool IsEntityID(uint8_t id)
{
    return id < EntityIDMax;
}

/**
 * @brief   Converts a RemoteCommandID to its string representation.
 */
constexpr const char* RemoteCommandIDtoString(uint8_t id)
{
    constexpr const char* kStrings[] = {
        "Play", "Pause", "TogglePlayPause", "NextTrack", "PreviousTrack",
        "VolumeUp", "VolumeDown", "AdvanceRepeatMode", "AdvanceShuffleMode",
        "SkipForward", "SkipBackward", "LikeTrack", "DislikeTrack",
        "BookmarkTrack"
    };

    if (id < sizeof(kStrings) / sizeof(kStrings[0])) {
        return kStrings[id];
    }
    return "Unknown RemoteCommandID";
}

/**
 * @brief   Converts an EntityID to its string representation.
 */
constexpr const char* EntityIDtoString(uint8_t id)
{
    constexpr const char* kStrings[] = { "Player", "Queue", "Track" };

    if (id < sizeof(kStrings) / sizeof(kStrings[0])) {
        return kStrings[id];
    }
    return "Unknown EntityID";
}

/**
 * @brief   Converts a PlaybackState to its string representation.
 */
constexpr const char* PlaybackStatetoString(uint8_t state)
{
    constexpr const char* kStrings[] = {
        "Paused", "Playing", "Rewinding", "FastForwarding"
    };

    if (state < sizeof(kStrings) / sizeof(kStrings[0])) {
        return kStrings[state];
    }
    return "Unknown PlaybackState";
}

/**
 * @struct  EntityUpdate
 * @brief   A single parsed Entity Update notification (one attribute).
 */
struct EntityUpdate {
    uint8_t entityID;          ///< EntityID the value belongs to.
    uint8_t attributeID;       ///< Attribute within the entity.
    uint8_t flags;             ///< EntityUpdateFlags bitmask.
    bool truncated;            ///< Value was truncated by the server.
    std::string_view value;    ///< UTF-8 value (view into the input buffer).
};

/**
 * @brief   Parses an Entity Update notification.
 *
 * Layout: EntityID(1) + AttributeID(1) + Flags(1) + Value(UTF-8, variable).
 *
 * @param   message: pointer to the notification bytes.
 * @param   len: length of the notification in bytes.
 * @param   out: parsed result; 'value' points into 'message'.
 * @return  True on success, false if the message is too short.
 */
bool ParseEntityUpdate(const uint8_t* message, uint16_t len, EntityUpdate& out);

/**
 * @brief   Parses a PlayerAttributeIDPlaybackInfo value "State,Rate,ElapsedTime".
 *
 * Rate and ElapsedTime may be absent (e.g. "0,," while paused); absent fields
 * default to 0. State defaults to PlaybackStatePaused.
 *
 * @param   value: the PlaybackInfo CSV string.
 * @param   state: parsed PlaybackState (out).
 * @param   rate: parsed playback rate (out, 0 if absent).
 * @param   elapsedSec: parsed elapsed time in seconds (out, 0 if absent).
 * @param   ratePresent: optional, set true if the rate field was present.
 * @param   elapsedPresent: optional, set true if the elapsed field was present.
 * @return  True if the value was non-empty, false otherwise.
 * @note    The player omits rate/elapsed while paused ("0,,"); callers that
 *          track progress should keep their last elapsed when it is absent
 *          rather than reset to 0.
 */
bool ParsePlaybackInfo(std::string_view value, uint8_t& state, float& rate,
        float& elapsedSec, bool* ratePresent = nullptr,
        bool* elapsedPresent = nullptr);

/**
 * @brief   Builds a Remote Command write (a single command byte).
 *
 * @param   cmd: command to send.
 * @param   buff: output buffer.
 * @param   buffSize: size of the output buffer.
 * @return  Number of bytes written (1), or 0 on error.
 */
uint16_t BuildRemoteCommand(RemoteCommandID cmd, uint8_t* buff,
        uint16_t buffSize);

/**
 * @brief   Builds an Entity Update registration write.
 *
 * Layout: EntityID(1) + AttributeID(1) per requested attribute.
 *
 * @param   entityID: entity to register interest in.
 * @param   attrs: array of attribute IDs.
 * @param   attrCount: number of attribute IDs.
 * @param   buff: output buffer.
 * @param   buffSize: size of the output buffer.
 * @return  Total length of the constructed packet, or 0 on error.
 */
uint16_t BuildEntityUpdateRegistration(uint8_t entityID, const uint8_t* attrs,
        uint16_t attrCount, uint8_t* buff, uint16_t buffSize);

} // namespace AMS

} // namespace Ble

#endif // BLE_AMS_PARSER_AMS_HPP
