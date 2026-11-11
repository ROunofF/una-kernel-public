/**
 ******************************************************************************
 * @file    NowPlaying.hpp
 * @brief   Now Playing media-state snapshot from the phone.
 ******************************************************************************
 * Plain media-state carrier filled by MediaService from Apple Media Service
 * (AMS) entity updates and passed to the GUI. It is platform-agnostic and
 * carries no AMS/BLE types, so the GUI can depend on it directly.
 *
 * Players report very different subsets (one reports elapsed/duration but no
 * pause; another reports pause/volume but no elapsed/duration). So every field
 * that may be absent is std::optional / State::Unknown / empty string: a value
 * is set only when the phone actually reports it, and the screen renders only
 * what is known. There is no local extrapolation here.
 ******************************************************************************
 */

#ifndef __NOW_PLAYING_HPP
#define __NOW_PLAYING_HPP

#include <cstdint>
#include <optional>
#include <string>

/**
 * @brief   Media remote-control commands sent from the GUI to the phone player.
 * @note    Values mirror Ble::AMS::RemoteCommandID so the value carried in
 *          G2BEvent::MediaRemoteCommand maps straight through.
 */
enum class MediaCommand : uint8_t {
    Play             = 0,
    Pause            = 1,
    TogglePlayPause  = 2,
    NextTrack        = 3,
    PreviousTrack    = 4,
    VolumeUp         = 5,
    VolumeDown       = 6,
};

/**
 * @brief   Snapshot of the phone's media player state. All strings are UTF-8.
 * @note    Absent fields stay nullopt / State::Unknown / empty - they are not
 *          defaulted to 0, so the consumer can tell "not reported" from a real
 *          zero (e.g. elapsed 0 at a track start, volume 0 when muted).
 */
struct NowPlaying {

    ///< Player playback state. Numeric values mirror AMS PlaybackState; Unknown
    ///< marks "not reported".
    enum class State : uint8_t {
        Paused          = 0,
        Playing         = 1,
        Rewinding       = 2,
        FastForwarding  = 3,
        Unknown         = 255,
    };

    // --- Track ------------------------------------------------------------
    std::string artist;                  ///< Track artist (empty = not reported).
    std::string album;                   ///< Track album (empty = not reported).
    std::string title;                   ///< Track title (empty = not reported).
    std::optional<float> durationSec;    ///< Track duration in seconds.

    // --- Player -----------------------------------------------------------
    std::string playerName;              ///< Active player application name.
    State state { State::Unknown };      ///< Playback state.
    std::optional<float> rate;           ///< Playback rate (1.0 = normal speed).
    std::optional<float> elapsedSec;     ///< Elapsed playback time in seconds.
    ///< Freshness token: bumped only when a NEW elapsed is reported (the value
    ///< can repeat, e.g. 0 across tracks). The consumer re-syncs its local
    ///< extrapolation when this changes, and keeps extrapolating when a snapshot
    ///< merely carries the last elapsed (e.g. on a volume change).
    uint32_t elapsedSeq { 0 };
    ///< Freshness token bumped on EVERY PlaybackInfo (state is always present,
    ///< even when elapsed is not, e.g. "0,," on pause). Lets the consumer act on
    ///< a fresh state report independently of elapsed, and ignore re-emits that
    ///< merely carry the last state (e.g. a volume change).
    uint32_t playbackSeq { 0 };
    std::optional<float> volume;         ///< Player volume, 0.0 .. 1.0.

    // --- Queue ------------------------------------------------------------
    std::optional<int32_t> queueIndex;   ///< Index of the current track in the queue.
    std::optional<int32_t> queueCount;   ///< Number of tracks in the queue.
    std::optional<int32_t> shuffleMode;  ///< AMS shuffle mode (0 off, 1 one, 2 all).
    std::optional<int32_t> repeatMode;   ///< AMS repeat mode (0 off, 1 one, 2 all).
};

#endif // __NOW_PLAYING_HPP
