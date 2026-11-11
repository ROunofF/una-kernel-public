/**
 ******************************************************************************
 * @file    ParserANCS.cpp
 * @brief   Data parser for The Apple Notification Center Service (ANCS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ParserANCS.hpp"

#define LOG_MODULE_PRX      "BLE.ParserANCS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Ble
{

namespace ANCS
{

bool ParseNotificationSource(const uint8_t *message, uint16_t len,
        NotificationSource &ns)
{
    // A Notification Source packet is 8 bytes: EventID, EventFlags, CategoryID,
    // CategoryCount, then the 4-byte NotificationUID. The length is peer-supplied,
    // so guard the full 8 read below (message[4]..message[7]).
    if (message == nullptr || len < 8) {
        return false;
    }

    ns.EventID = message[0];
    ns.EventIDS = EventIDtoString(ns.EventID);
    ns.EventFlags = message[1];
    ns.CategoryID = message[2];
    ns.CategoryIDS = CategoryIDtoString(ns.CategoryID);
    ns.CategoryCount = message[3];
    ns.NotificationUID = (message[4]) | (message[5] << 8) |
            (message[6] << 16) | (message[7] << 24);

    return true;
}

uint16_t CreateAttributeRequest(uint32_t uid, const AttrInfo *attrs,
        uint16_t attrCount, uint8_t *buff, uint16_t buffSize)
{
    if (buff == nullptr || attrs == nullptr || buffSize < 5) {
        return 0; // Invalid input or insufficient buffer size
    }

    uint16_t offset = 0;

    // Add Command ID
    buff[offset++] = CommandIDGetNotificationAttributes;

    // Add Notification UID (4 bytes, little-endian)
    buff[offset++] = static_cast<uint8_t>(uid & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 8) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 16) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 24) & 0xFF);

    // Add attributes
    for (uint16_t i = 0; i < attrCount; ++i) {
        if (!IsAttributeID(attrs[i].id)) {
            return 0; // Invalid attribute ID
        }
        if (offset + 1 > buffSize) {
            return 0; // Insufficient buffer size
        }

        // Add attribute ID
        buff[offset++] = attrs[i].id;

        // Add max length if required (2 bytes, little-endian)
        if (attrs[i].id == NotificationAttributeIDTitle ||
                attrs[i].id == NotificationAttributeIDSubtitle ||
                attrs[i].id == NotificationAttributeIDMessage)
        {
            if (offset + 2 > buffSize) {
                return 0; // Insufficient buffer size
            }
            buff[offset++] = static_cast<uint8_t>(attrs[i].maxLen & 0xFF);
            buff[offset++] = static_cast<uint8_t>((attrs[i].maxLen >> 8) & 0xFF);
        }
    }

    return offset; // Return total length of the constructed packet
}

uint16_t CreateActionRequest(uint32_t uid, ActionID action, uint8_t *buff,
        uint16_t buffSize)
{
    if (buff == nullptr || buffSize < 6) {
        return 0; // Invalid input or insufficient buffer size
    }

    uint16_t offset = 0;

    // Add Command ID
    buff[offset++] = CommandIDPerformNotificationAction;

    // Add Notification UID (4 bytes, little-endian)
    buff[offset++] = static_cast<uint8_t>(uid & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 8) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 16) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 24) & 0xFF);

    buff[offset++] = static_cast<uint8_t>(action);

    return offset; // Return total length of the constructed packet
}

std::tm ConvertDateToTm(const char *dateStr, size_t len)
{
    std::tm tm = { };

    if (!dateStr || len != 15 || dateStr[8] != 'T') {
        return tm;
    }

    if (sscanf(dateStr, "%4d%2d%2dT%2d%2d%2d", &tm.tm_year, &tm.tm_mon,
                &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
    {
        return tm;
    }

    // Adjust the fields of the tm structure
    tm.tm_year -= 1900; // Years from 1900
    tm.tm_mon -= 1;     // Months from 0 to 11

    return tm;
}

void AttributeParser::init(uint32_t uid, AttrInfo *attrs, uint16_t attrCount,
        uint8_t *workBuffer, uint16_t buffLength)
{
    mExpectedUid = uid;
    mAttrs = attrs;
    mAttrCount = attrCount;
    mBuffer = workBuffer;
    mBufferSize = buffLength;

    resetState();
}

AttributeParser::Status AttributeParser::appendData(const uint8_t *data,
        uint16_t length)
{
    if (length > (mBufferSize - mBufferUsed)) {
        return Status::ERROR; // Not enough space in the buffer
    }

    // Copy data into the buffer
    std::memcpy(mBuffer + mBufferUsed, data, length);
    mBufferUsed += length;

    // Process the buffer
    return parseBuffer();
}

void AttributeParser::reset()
{
    mExpectedUid = 0;
    mAttrs = nullptr;
    mAttrCount = 0;
    mBuffer = nullptr;
    mBufferSize = 0;

    resetState();
}

void AttributeParser::resetState() {
    mBufferUsed = 0;
    mCurrentOffset = 0;
    mHasParsedHeader = false;
    mCurrentAttr = nullptr;
    mCurrentAttrLengthParsed = false;
    mCurrentAttrLength = 0;
    mCurrentAttrDataReceived = 0;
    mCurrentAttrIndex = 0;
}

AttributeParser::Status AttributeParser::parseBuffer()
{
    if (mCurrentOffset >= mBufferUsed) {
        return Status::ERROR; // No data to process
    }

    const uint8_t *currentPtr = mBuffer + mCurrentOffset;
    size_t remainingLength = mBufferUsed - mCurrentOffset;

    // Parse the CommandID and UID from the first packet
    if (!mHasParsedHeader) {
        // Check the first byte before waiting for a full header: a short
        // chunk of a response that was already discarded as stale must not
        // linger in the buffer, or it corrupts the response appended after it.
        // (Best-effort: a stale chunk that happens to begin with the command
        // byte can still slip through and poison the next response; the fetch
        // timeout and retry recover from that.)
        uint8_t commandID = *currentPtr;
        if (commandID != CommandID::CommandIDGetNotificationAttributes) {
            // Not a response header: a continuation chunk of a response that
            // was already discarded as stale. Drop it and keep waiting for the
            // expected response; the fetch timeout bounds the wait.
            LOG_DEBUG("Discarding stale chunk (0x%02X)\n", commandID);
            resetState();
            return Status::OK;
        }

        if (remainingLength < 5) { // Minimum size: CommandID (1) + UID (4)
            return Status::OK; // Wait for more data
        }

        uint32_t uid = static_cast<uint32_t>(currentPtr[1])
                | (static_cast<uint32_t>(currentPtr[2]) << 8)
                | (static_cast<uint32_t>(currentPtr[3]) << 16)
                | (static_cast<uint32_t>(currentPtr[4]) << 24);

        if (uid != mExpectedUid) {
            // A late response to a previous request. Failing the current fetch
            // here keeps the pipe permanently one response behind; discard the
            // stale response instead and keep waiting for ours.
            LOG_DEBUG("Discarding stale response. Got UID %u, expecting %u\n",
                    uid, mExpectedUid);
            resetState();
            return Status::OK;
        }

        mHasParsedHeader = true;
        currentPtr += 5;
        remainingLength -= 5;
        mCurrentOffset += 5;
    }

    // Parse attributes
    while (remainingLength > 0) {
        if (!mCurrentAttr) {
            // Data beyond the final attribute (a duplicated or trailing
            // chunk): without this bound the loop would index one past the
            // caller's attribute array and write through it.
            if (mCurrentAttrIndex >= mAttrCount) {
                LOG_ERROR("Data beyond the final attribute\n");
                return Status::ERROR;
            }

            // Read the next attribute ID
            if (remainingLength < 1) {
                return Status::OK; // Wait for more data
            }

            uint8_t attrID = *currentPtr;

            if (attrID != mAttrs[mCurrentAttrIndex].id) {
                return Status::ERROR; // Unknown attribute ID, or wrong order
            }

            currentPtr += 1;
            remainingLength -= 1;
            mCurrentOffset += 1;

            mCurrentAttr = &mAttrs[mCurrentAttrIndex];
            mCurrentAttr->strv = std::string_view(); // Reset the string view
        }

        // Read the length of the attribute
        if (!mCurrentAttrLengthParsed) {
            if (remainingLength < 2) {
                return Status::OK; // Wait for more data
            }

            mCurrentAttrLength = static_cast<uint16_t>(currentPtr[0])
                    | (static_cast<uint16_t>(currentPtr[1]) << 8);

            // Validate length against maxLen (if applicable)
            if (mCurrentAttr->maxLen > 0 && mCurrentAttrLength
                    > mCurrentAttr->maxLen) {
                return Status::ERROR; // Attribute length exceeds maxLen
            }

            currentPtr += 2;
            remainingLength -= 2;
            mCurrentOffset += 2;
            mCurrentAttrLengthParsed = true;
        }

        // Read the attribute data
        size_t dataToRead = std::min(
                static_cast<size_t>(mCurrentAttrLength
                        - mCurrentAttrDataReceived), remainingLength);
        if (dataToRead > 0) {
            if (mCurrentAttrDataReceived == 0) {
                // Set the string view to point to the start of this attribute's data
                mCurrentAttr->strv = std::string_view(
                        reinterpret_cast<const char*>(currentPtr), dataToRead);
            } else {
                // Extend the string view to include the new data
                mCurrentAttr->strv = std::string_view(mCurrentAttr->strv.data(),
                        mCurrentAttr->strv.size() + dataToRead);
            }

            currentPtr += dataToRead;
            remainingLength -= dataToRead;
            mCurrentOffset += dataToRead;
            mCurrentAttrDataReceived += dataToRead;
        }

        // If the attribute is fully received, reset state for the next attribute
        if (mCurrentAttrDataReceived == mCurrentAttrLength) {
            mCurrentAttr = nullptr;
            mCurrentAttrLengthParsed = false;
            mCurrentAttrDataReceived = 0;
            mCurrentAttrIndex++; // next argument
        }
    }

    // If we've processed all attributes, return DONE
    return mCurrentAttrIndex == mAttrCount ? Status::DONE : Status::OK;
}

} /* namespace ANCS */

} /* namespace Ble */
