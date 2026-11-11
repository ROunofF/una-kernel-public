/**
 ******************************************************************************
 * @file    MediaService.cpp
 * @brief   Media Service: Now Playing consumer and remote-command source.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.SRV.Media"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "MediaService.hpp"

#include <cstring>
#include <cstdlib>

#include "OS.hpp"
#include "ParserAMS.hpp"
#include "ParserCAMS.hpp"

namespace Ble
{

namespace
{

float toFloat(std::string_view value)
{
    if (value.empty()) {
        return 0.0f;
    }
    char buf[24];
    size_t n = (value.size() < sizeof(buf) - 1) ? value.size() : sizeof(buf) - 1;
    memcpy(buf, value.data(), n);
    buf[n] = '\0';
    return strtof(buf, nullptr);
}

int32_t toInt(std::string_view value)
{
    if (value.empty()) {
        return 0;
    }
    char buf[24];
    size_t n = (value.size() < sizeof(buf) - 1) ? value.size() : sizeof(buf) - 1;
    memcpy(buf, value.data(), n);
    buf[n] = '\0';
    return static_cast<int32_t>(strtol(buf, nullptr, 10));
}

} // namespace

MediaService::MediaService(Interface::IBle& ble, CustomCommandService& ccs) :
        mBle(ble), mCcs(ccs)
{
    // iOS: enable the AMS phone-link client and receive its entity updates.
    ClientAMS::GetInstance().init();
    ClientAMS::GetInstance().attachAMSCallback(this);

    // Android: receive Now Playing updates from the custom command service.
    mCcs.setMediaCommandSink(this);

    mBle.attachStatusCallback(this);
}

MediaService::~MediaService()
{
    mBle.detachStatusCallback(this);
    mCcs.setMediaCommandSink(nullptr);
    ClientAMS::GetInstance().attachAMSCallback(nullptr);
}

void MediaService::attachCallback(Callback* pCallback)
{
    mObserver.add(pCallback);
}

void MediaService::detachCallback(Callback* pCallback)
{
    mObserver.del(pCallback);
}

bool MediaService::sendRemoteCommand(uint8_t command)
{
    LOG_DEBUG("Remote command %u\n", command);

    // Pick a single transport. When an AMS (iOS) media link is resolved, send
    // only over AMS: the watch's own CCS Event characteristic can stay subscribed
    // on an iOS phone for other events, and pushing CAMS remote commands into it
    // would spam a link that does not speak the Android media protocol.
    ClientAMS& ams = ClientAMS::GetInstance();
    if (ams.isMediaLinkActive()) {
        return ams.writeRemoteCommand(command);
    }
    return mCcs.sendEventMediaRemoteCommand(command);
}

void MediaService::notifyNowPlaying()
{
    // Publish an immutable snapshot of the working copy. Updates are sparse, so a
    // fresh allocation per change is fine (and the consumer keeps the last one,
    // so the object cannot be reused in place anyway).
    auto snapshot = std::make_shared<NowPlaying>(mNowPlaying);
    if (snapshot) {
        mObserver.notify(&MediaService::Callback::onNowPlaying, snapshot);
    }
}

void MediaService::onRemoteCommandSupported(const uint8_t* pData, uint16_t len)
{
    // The set of currently available commands. Not surfaced yet; the consumer
    // may always offer the full control set.
    (void) pData;
    LOG_DEBUG("Remote command list, len=%u\n", len);
}

void MediaService::onEntityUpdate(const uint8_t* pData, uint16_t len)
{
    AMS::EntityUpdate eu {};
    if (!AMS::ParseEntityUpdate(pData, len, eu)) {
        return;
    }

    switch (eu.entityID) {
        case AMS::EntityIDTrack:
            switch (eu.attributeID) {
                case AMS::TrackAttributeIDArtist:
                    mNowPlaying.artist.assign(eu.value);
                    break;
                case AMS::TrackAttributeIDAlbum:
                    mNowPlaying.album.assign(eu.value);
                    break;
                case AMS::TrackAttributeIDTitle:
                    mNowPlaying.title.assign(eu.value);
                    break;
                case AMS::TrackAttributeIDDuration:
                    mNowPlaying.durationSec = toFloat(eu.value);
                    break;
                default:
                    return;
            }
            break;

        case AMS::EntityIDPlayer:
            switch (eu.attributeID) {
                case AMS::PlayerAttributeIDName:
                    // A different player took over: drop the previous player's
                    // stale fields (e.g. elapsed/duration the new one may never
                    // report) so nothing leaks across the switch; keep the name.
                    if (mNowPlaying.playerName.compare(eu.value) != 0) {
                        mNowPlaying = NowPlaying {};
                        mNowPlaying.playerName.assign(eu.value);
                    }
                    break;
                case AMS::PlayerAttributeIDPlaybackInfo: {
                    uint8_t state = AMS::PlaybackStatePaused;
                    float rate = 0.0f;
                    float elapsed = 0.0f;
                    bool ratePresent = false;
                    bool elapsedPresent = false;
                    AMS::ParsePlaybackInfo(eu.value, state, rate, elapsed,
                            &ratePresent, &elapsedPresent);
                    // State is always present; rate/elapsed only when reported
                    // (absent while paused on some players) - keep the last value
                    // so progress does not reset to 0.
                    mNowPlaying.playbackSeq = ++mPlaybackSeq;  // fresh state report
                    mNowPlaying.state = static_cast<NowPlaying::State>(state);
                    if (ratePresent) {
                        mNowPlaying.rate = rate;
                    }
                    if (elapsedPresent) {
                        mNowPlaying.elapsedSec = elapsed;
                        mNowPlaying.elapsedSeq = ++mElapsedSeq;  // mark fresh
                    }
                    break;
                }
                case AMS::PlayerAttributeIDVolume:
                    mNowPlaying.volume = toFloat(eu.value);
                    break;
                default:
                    return;
            }
            break;

        case AMS::EntityIDQueue:
            switch (eu.attributeID) {
                case AMS::QueueAttributeIDIndex:
                    mNowPlaying.queueIndex = toInt(eu.value);
                    break;
                case AMS::QueueAttributeIDCount:
                    mNowPlaying.queueCount = toInt(eu.value);
                    break;
                case AMS::QueueAttributeIDShuffleMode:
                    mNowPlaying.shuffleMode = toInt(eu.value);
                    break;
                case AMS::QueueAttributeIDRepeatMode:
                    mNowPlaying.repeatMode = toInt(eu.value);
                    break;
                default:
                    return;
            }
            break;

        default:
            return;
    }

    notifyNowPlaying();
}

bool MediaService::onMediaNowPlaying(const uint8_t* pData, uint16_t len)
{
    CAMS::MediaUpdate u {};
    if (!CAMS::ParseNowPlayingUpdate(pData, len, u)) {
        return false;
    }

    // A different player took over: drop the previous player's stale fields (e.g.
    // elapsed/duration the new one may never report) before applying the rest of
    // this update, so nothing leaks across the switch; keep the name.
    if (u.playerName && mNowPlaying.playerName.compare(*u.playerName) != 0) {
        mNowPlaying = NowPlaying {};
        mNowPlaying.playerName.assign(*u.playerName);
    }

    if (u.title) { mNowPlaying.title.assign(*u.title); }
    if (u.artist) { mNowPlaying.artist.assign(*u.artist); }
    if (u.album) { mNowPlaying.album.assign(*u.album); }
    if (u.durationSec) { mNowPlaying.durationSec = static_cast<float>(*u.durationSec); }

    // State is bumped on every report; elapsed marks a fresh position. These two
    // freshness tokens drive the screen's local progress extrapolation.
    if (u.state) {
        mNowPlaying.playbackSeq = ++mPlaybackSeq;
        mNowPlaying.state = static_cast<NowPlaying::State>(*u.state);
    }
    if (u.elapsedSec) {
        mNowPlaying.elapsedSec = static_cast<float>(*u.elapsedSec);
        mNowPlaying.elapsedSeq = ++mElapsedSeq;
    }
    if (u.rateCenti) { mNowPlaying.rate = *u.rateCenti / 100.0f; }
    if (u.volumePct) { mNowPlaying.volume = *u.volumePct / 100.0f; }
    if (u.queueIndex) { mNowPlaying.queueIndex = *u.queueIndex; }
    if (u.queueCount) { mNowPlaying.queueCount = *u.queueCount; }
    if (u.shuffleMode) { mNowPlaying.shuffleMode = *u.shuffleMode; }
    if (u.repeatMode) { mNowPlaying.repeatMode = *u.repeatMode; }

    notifyNowPlaying();
    return true;
}

void MediaService::onDisconnect(uint8_t reason)
{
    (void) reason;
    // Drop the stale media state and let the consumer clear its view.
    mNowPlaying = NowPlaying {};
    notifyNowPlaying();
}

} // namespace Ble
