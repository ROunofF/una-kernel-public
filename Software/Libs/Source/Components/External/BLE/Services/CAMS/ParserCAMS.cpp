/**
 ******************************************************************************
 * @file    ParserCAMS.cpp
 * @brief   Data parser for the Custom Android Media Service (CAMS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ParserCAMS.hpp"

namespace Ble
{

namespace CAMS
{

namespace
{

uint16_t readU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string_view view(const uint8_t* p, uint8_t len)
{
    return std::string_view(reinterpret_cast<const char*>(p), len);
}

} // namespace

bool ParseNowPlayingUpdate(const uint8_t* msg, uint16_t len, MediaUpdate& out)
{
    out = MediaUpdate {};

    if (msg == nullptr || len < kNowPlayingHeaderLen) {
        return false;
    }
    if (msg[0] != kCmdNowPlaying) {
        return false;
    }

    uint16_t i = kNowPlayingHeaderLen;
    while (i < len) {
        // Each entry needs an attribute id and a length byte.
        if (i + 2 > len) {
            return false;
        }
        uint8_t attrId = msg[i];
        uint8_t valLen = msg[i + 1];
        uint16_t valOff = static_cast<uint16_t>(i + 2);
        if (valOff + valLen > len) {
            return false;
        }

        const uint8_t* v = &msg[valOff];
        switch (attrId) {
            case MediaAttributeIDTitle:      out.title = view(v, valLen); break;
            case MediaAttributeIDArtist:     out.artist = view(v, valLen); break;
            case MediaAttributeIDAlbum:      out.album = view(v, valLen); break;
            case MediaAttributeIDPlayerName: out.playerName = view(v, valLen); break;
            case MediaAttributeIDDuration:
                if (valLen == 4) { out.durationSec = readU32(v); }
                break;
            case MediaAttributeIDElapsed:
                if (valLen == 4) { out.elapsedSec = readU32(v); }
                break;
            case MediaAttributeIDState:
                if (valLen == 1) { out.state = v[0]; }
                break;
            case MediaAttributeIDRate:
                if (valLen == 2) { out.rateCenti = readU16(v); }
                break;
            case MediaAttributeIDVolume:
                if (valLen == 1) { out.volumePct = v[0]; }
                break;
            case MediaAttributeIDQueueIndex:
                if (valLen == 2) { out.queueIndex = readU16(v); }
                break;
            case MediaAttributeIDQueueCount:
                if (valLen == 2) { out.queueCount = readU16(v); }
                break;
            case MediaAttributeIDShuffleMode:
                if (valLen == 1) { out.shuffleMode = v[0]; }
                break;
            case MediaAttributeIDRepeatMode:
                if (valLen == 1) { out.repeatMode = v[0]; }
                break;
            default:
                // Unknown attribute: skip it (the length lets us advance).
                break;
        }

        i = static_cast<uint16_t>(valOff + valLen);
    }

    return true;
}

uint16_t BuildRemoteCommandEvent(uint8_t cmd, uint8_t* buff, uint16_t buffSize)
{
    if (buff == nullptr || buffSize < kRemoteCommandEventLen) {
        return 0;
    }

    buff[0] = kEvtRemoteCommand;
    buff[1] = cmd;
    return kRemoteCommandEventLen;
}

} // namespace CAMS

} // namespace Ble
