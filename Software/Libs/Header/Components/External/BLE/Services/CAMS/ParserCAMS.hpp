/**
 ******************************************************************************
 * @file    ParserCAMS.hpp
 * @brief   Data parser for the Custom Android Media Service (CAMS).
 ******************************************************************************
 *
 * Pure data helpers for the Android media protocol carried over the Custom
 * Communication Service: parse a Now Playing update (command 0x20) into a
 * presence-aware MediaUpdate, and build a remote-command event (0x03). No
 * controller-stack, OS or logging dependency, so this unit is host-testable.
 *
 * Wire format is specified in CAMS.md next to this file.
 *
 ******************************************************************************
 */

#ifndef BLE_CAMS_PARSER_CAMS_HPP
#define BLE_CAMS_PARSER_CAMS_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace Ble
{

namespace CAMS
{

///< CCS command carrying a Now Playing update (phone -> watch).
constexpr uint8_t kCmdNowPlaying = 0x20;

///< CCS event carrying a remote command (watch -> phone).
constexpr uint8_t kEvtRemoteCommand = 0x03;

///< Now Playing header: command id + reserved byte, before the attribute list.
constexpr uint16_t kNowPlayingHeaderLen = 2;

///< Length of a remote-command event payload: event id + command.
constexpr uint16_t kRemoteCommandEventLen = 2;

// MediaCommandID values (carried in a remote-command event). Numeric values
// mirror Ble::AMS::RemoteCommandID and MediaCommand so they map straight through.
enum MediaCommandID {
    MediaCommandIDPlay            = 0,
    MediaCommandIDPause           = 1,
    MediaCommandIDTogglePlayPause = 2,
    MediaCommandIDNextTrack       = 3,
    MediaCommandIDPreviousTrack   = 4,
    MediaCommandIDVolumeUp        = 5,
    MediaCommandIDVolumeDown      = 6,

    MediaCommandIDMax
};

// MediaAttributeID values (the TLV type in a Now Playing update).
enum MediaAttributeID {
    MediaAttributeIDTitle       = 0x01,  // UTF-8 string
    MediaAttributeIDArtist      = 0x02,  // UTF-8 string
    MediaAttributeIDAlbum       = 0x03,  // UTF-8 string
    MediaAttributeIDDuration    = 0x04,  // uint32 LE, seconds
    MediaAttributeIDPlayerName  = 0x05,  // UTF-8 string
    MediaAttributeIDState       = 0x06,  // uint8 PlaybackState
    MediaAttributeIDElapsed     = 0x07,  // uint32 LE, seconds
    MediaAttributeIDRate        = 0x08,  // uint16 LE, hundredths (100 = 1.00x)
    MediaAttributeIDVolume      = 0x09,  // uint8 percent, 0..100
    MediaAttributeIDQueueIndex  = 0x0A,  // uint16 LE
    MediaAttributeIDQueueCount  = 0x0B,  // uint16 LE
    MediaAttributeIDShuffleMode = 0x0C,  // uint8 (0 off, 1 one, 2 all)
    MediaAttributeIDRepeatMode  = 0x0D,  // uint8 (0 off, 1 one, 2 all)
};

// PlaybackState, the value of MediaAttributeIDState. Mirrors NowPlaying::State.
enum PlaybackState {
    PlaybackStatePaused         = 0,
    PlaybackStatePlaying        = 1,
    PlaybackStateRewinding      = 2,
    PlaybackStateFastForwarding = 3,
};

/**
 * @brief   Checks whether an ID is a valid MediaCommandID.
 */
constexpr bool IsMediaCommandID(uint8_t id)
{
    return id < MediaCommandIDMax;
}

/**
 * @struct  MediaUpdate
 * @brief   A parsed Now Playing update.
 * @note    Presence-aware: each field is set only if the packet carried it, so a
 *          consumer can leave its last value unchanged for absent fields. String
 *          fields are views into the input buffer (valid while it lives).
 */
struct MediaUpdate {
    std::optional<std::string_view> title;
    std::optional<std::string_view> artist;
    std::optional<std::string_view> album;
    std::optional<std::string_view> playerName;
    std::optional<uint32_t> durationSec;
    std::optional<uint32_t> elapsedSec;
    std::optional<uint8_t> state;        ///< PlaybackState.
    std::optional<uint16_t> rateCenti;   ///< Playback rate in hundredths.
    std::optional<uint8_t> volumePct;    ///< Volume, percent 0..100.
    std::optional<uint16_t> queueIndex;
    std::optional<uint16_t> queueCount;
    std::optional<uint8_t> shuffleMode;
    std::optional<uint8_t> repeatMode;
};

/**
 * @brief   Parses a Now Playing update packet.
 *
 * Layout: CommandID(0x20) + Reserved(0) + [AttrID(1) Len(1) Value(Len)]...
 * Unknown attribute IDs and numeric attributes of unexpected length are skipped;
 * a TLV whose length runs past the buffer is a hard error.
 *
 * @param   msg: pointer to the full packet (including the 2-byte header).
 * @param   len: length of the packet in bytes.
 * @param   out: parsed result; string fields point into 'msg'.
 * @return  True on success, false if the header is wrong or a TLV is malformed.
 */
bool ParseNowPlayingUpdate(const uint8_t* msg, uint16_t len, MediaUpdate& out);

/**
 * @brief   Builds a remote-command event payload.
 *
 * Layout: EventID(0x03) + Command(1).
 *
 * @param   cmd: a MediaCommandID value.
 * @param   buff: output buffer.
 * @param   buffSize: size of the output buffer.
 * @return  Number of bytes written (2), or 0 on error.
 */
uint16_t BuildRemoteCommandEvent(uint8_t cmd, uint8_t* buff, uint16_t buffSize);

} // namespace CAMS

} // namespace Ble

#endif // BLE_CAMS_PARSER_CAMS_HPP
