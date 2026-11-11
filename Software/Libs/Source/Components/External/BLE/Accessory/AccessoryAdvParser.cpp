/**
 ******************************************************************************
 * @file    AccessoryAdvParser.cpp
 * @brief   Advertising-data parsing for accessory discovery.
 ******************************************************************************
 *
 * Pure parsing logic only (no BlueNRG/OS deps) so it is host-unit-tested.
 * Behaviour is identical to the anonymous-namespace helpers this replaces in
 * AccessoryCentral.cpp, apart from the added multi-UUID match.
 *
 ******************************************************************************
 */

#include "AccessoryAdvParser.hpp"

#include <cstring>

namespace Ble
{

namespace {

// AD types we care about (Bluetooth Core Supplement, Part A).
constexpr uint8_t kAdTypeUuid16Incomplete = 0x02;
constexpr uint8_t kAdTypeUuid16Complete   = 0x03;
constexpr uint8_t kAdTypeNameShortened    = 0x08;
constexpr uint8_t kAdTypeNameComplete     = 0x09;

}  // namespace

bool advertisesAnyService(const uint8_t *data, uint8_t len,
        const uint16_t *uuids, uint8_t count, uint16_t *matchedOut)
{
    if (data == nullptr || uuids == nullptr || count == 0) {
        return false;
    }

    uint8_t i = 0;
    // i + 1 < len: an AD structure needs at least a length and a type byte.
    while (i + 1 < len) {
        const uint8_t fieldLen = data[i];
        // Zero length terminates the list; a length running past the end is
        // malformed -- stop either way rather than reading out of bounds.
        if (fieldLen == 0 || i + 1 + fieldLen > len) {
            break;
        }
        const uint8_t adType = data[i + 1];

        if (adType == kAdTypeUuid16Incomplete || adType == kAdTypeUuid16Complete) {
            const uint8_t *entries = &data[i + 2];
            // fieldLen counts the type byte, so the payload is one shorter.
            const uint8_t entryBytes = static_cast<uint8_t>(fieldLen - 1);
            for (uint8_t j = 0; j + 1 < entryBytes; j += 2) {
                const uint16_t uuid = static_cast<uint16_t>(entries[j] |
                        (static_cast<uint16_t>(entries[j + 1]) << 8));
                for (uint8_t k = 0; k < count; ++k) {
                    if (uuid == uuids[k]) {
                        if (matchedOut != nullptr) {
                            *matchedOut = uuid;
                        }
                        return true;
                    }
                }
            }
        }

        i = static_cast<uint8_t>(i + fieldLen + 1);
    }
    return false;
}

bool advertisesService(const uint8_t *data, uint8_t len, uint16_t serviceUuid)
{
    return advertisesAnyService(data, len, &serviceUuid, 1, nullptr);
}

void parseAdvName(const uint8_t *data, uint8_t len, char *out, uint8_t outSize)
{
    if (out == nullptr || outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (data == nullptr) {
        return;
    }

    uint8_t i = 0;
    while (i + 1 < len) {
        const uint8_t fieldLen = data[i];
        if (fieldLen == 0 || i + 1 + fieldLen > len) {
            break;
        }
        const uint8_t adType = data[i + 1];

        if (adType == kAdTypeNameShortened || adType == kAdTypeNameComplete) {
            uint8_t nameLen = static_cast<uint8_t>(fieldLen - 1);
            // Advertised names can exceed the buffers that display them, so
            // truncate rather than overflow.
            if (nameLen >= outSize) {
                nameLen = static_cast<uint8_t>(outSize - 1);
            }
            memcpy(out, &data[i + 2], nameLen);
            out[nameLen] = '\0';
            return;
        }

        i = static_cast<uint8_t>(i + fieldLen + 1);
    }
}

}  /* namespace Ble */
