/**
 ******************************************************************************
 * @file    ParserAMS.cpp
 * @brief   Data parser for the Apple Media Service (AMS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ParserAMS.hpp"

#include <cstring>
#include <cstdlib>

namespace Ble
{

namespace AMS
{

///< Minimum Entity Update length: EntityID + AttributeID + Flags.
static constexpr uint16_t kEntityUpdateHeaderLen = 3;

bool ParseEntityUpdate(const uint8_t* message, uint16_t len, EntityUpdate& out)
{
    if (message == nullptr || len < kEntityUpdateHeaderLen) {
        return false;
    }

    out.entityID = message[0];
    out.attributeID = message[1];
    out.flags = message[2];
    out.truncated = (out.flags & EntityUpdateFlagTruncated) != 0;
    out.value = std::string_view(
            reinterpret_cast<const char*>(&message[kEntityUpdateHeaderLen]),
            len - kEntityUpdateHeaderLen);

    return true;
}

bool ParsePlaybackInfo(std::string_view value, uint8_t& state, float& rate,
        float& elapsedSec, bool* ratePresent, bool* elapsedPresent)
{
    state = PlaybackStatePaused;
    rate = 0.0f;
    elapsedSec = 0.0f;
    if (ratePresent != nullptr) {
        *ratePresent = false;
    }
    if (elapsedPresent != nullptr) {
        *elapsedPresent = false;
    }

    if (value.empty()) {
        return false;
    }

    // The view is not NUL-terminated; copy into a bounded buffer to parse.
    char buf[32];
    size_t n = (value.size() < sizeof(buf) - 1) ? value.size() : sizeof(buf) - 1;
    memcpy(buf, value.data(), n);
    buf[n] = '\0';

    // "State,Rate,ElapsedTime"; Rate and ElapsedTime may be empty ("0,,").
    float* const fields[] = { nullptr, &rate, &elapsedSec };
    bool* const present[] = { nullptr, ratePresent, elapsedPresent };
    char* cursor = buf;
    for (uint8_t i = 0; i < 3 && cursor != nullptr; i++) {
        char* comma = strchr(cursor, ',');
        if (comma != nullptr) {
            *comma = '\0';
        }
        if (*cursor != '\0') {
            if (i == 0) {
                state = static_cast<uint8_t>(strtol(cursor, nullptr, 10));
            } else {
                *fields[i] = strtof(cursor, nullptr);
                if (present[i] != nullptr) {
                    *present[i] = true;
                }
            }
        }
        cursor = (comma != nullptr) ? comma + 1 : nullptr;
    }

    return true;
}

uint16_t BuildRemoteCommand(RemoteCommandID cmd, uint8_t* buff,
        uint16_t buffSize)
{
    if (buff == nullptr || buffSize < 1) {
        return 0;
    }

    buff[0] = static_cast<uint8_t>(cmd);
    return 1;
}

uint16_t BuildEntityUpdateRegistration(uint8_t entityID, const uint8_t* attrs,
        uint16_t attrCount, uint8_t* buff, uint16_t buffSize)
{
    if (buff == nullptr || attrs == nullptr || attrCount == 0) {
        return 0;
    }

    uint16_t total = static_cast<uint16_t>(1 + attrCount);
    if (total > buffSize) {
        return 0;
    }

    buff[0] = entityID;
    for (uint16_t i = 0; i < attrCount; i++) {
        buff[1 + i] = attrs[i];
    }

    return total;
}

} // namespace AMS

} // namespace Ble
