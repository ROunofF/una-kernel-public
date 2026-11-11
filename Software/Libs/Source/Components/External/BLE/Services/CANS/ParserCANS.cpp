/**
 ******************************************************************************
 * @file    ParserCANS.сpp
 * @brief   Data parser for Custom Android Notification Service (CANS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ParserCANS.hpp"

#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <cstdio>

#define LOG_MODULE_PRX      "BLE.ParserCANS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

namespace Ble
{

namespace CANS
{

bool ParseNotification(const uint8_t *message, uint16_t len, Notification &ns)
{
    if (message == nullptr || len < 7) {
        return false;
    }

    ns.protocol = message[0];
    ns.action = message[1];

    ns.uid = (message[2]) | (message[3] << 8) | (message[4] << 16)
             | (message[5] << 24);

    ns.category = message[6];

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
    buff[offset++] = CommandID::COMMAND_ID_REQUEST_ATTRIBUTES;

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
        if (offset + 3 > buffSize) {
            return 0; // Insufficient buffer size
        }

        // Add attribute ID
        buff[offset++] = attrs[i].id;

        buff[offset++] = static_cast<uint8_t>(attrs[i].maxLen & 0xFF);
        buff[offset++] = static_cast<uint8_t>((attrs[i].maxLen >> 8) & 0xFF);

    }

    return offset; // Return total length of the constructed packet
}

uint16_t CreateActionRequest(uint32_t uid, CommandID cid, uint8_t *buff,
        uint16_t buffSize)
{
    if (buff == nullptr || buffSize < 5 ||
            !(cid == COMMAND_ID_EXECUTE_POSITIVE_ACTION || cid == COMMAND_ID_EXECUTE_NEGATIVE_ACTION))
    {
        return 0; // Invalid input or insufficient buffer size
    }

    uint16_t offset = 0;

    // Add Command ID
    buff[offset++] = static_cast<uint8_t>(cid);

    // Add Notification UID (4 bytes, little-endian)
    buff[offset++] = static_cast<uint8_t>(uid & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 8) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 16) & 0xFF);
    buff[offset++] = static_cast<uint8_t>((uid >> 24) & 0xFF);

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
    resetState();

    mExpectedUid = uid;
    mAttrs = attrs;
    mAttrCount = attrCount;
    mBuffer = workBuffer;
    mBufferSize = buffLength;
}

AttributeParser::Status AttributeParser::appendData(const uint8_t *data,
        uint16_t length)
{
    if (mExpectedUid == 0) {
        LOG_ERROR("Parser not initialized\n");
        return Status::ERROR;
    }

    if (length > (mBufferSize - mBufferUsed)) {
        LOG_ERROR("Buffer too small: %d\n", mBufferSize);
        return Status::ERROR; // Not enough space in the buffer
    }

    // Check error
    if (mBufferUsed == 0 && length == 1 && IsErrorCode(*data)) {
        LOG_ERROR("Err code: 0x%02X\n", *data);
        return Status::ERROR;
    }

    // Copy data into the buffer
    std::memcpy(mBuffer + mBufferUsed, data, length);
    mBufferUsed += length;

    // Process the buffer
    return parseBuffer();
}

void AttributeParser::reset()
{
    resetState();
}

void AttributeParser::resetState() {

    mExpectedUid = 0;
    mAttrs = nullptr;
    mAttrCount = 0;
    mBuffer = nullptr;
    mBufferSize = 0;

    discardPending();
}

void AttributeParser::discardPending()
{
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
        LOG_ERROR("No data to process\n");
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
        // Genuine phone error responses are single-byte packets, rejected in
        // appendData() before they reach this buffer — a 0x80..0x82 first byte
        // here is UTF-8 continuation text from a discarded stale body, not an
        // error, so it must not fail the fetch.
        uint8_t commandID = *currentPtr;
        if (commandID != CommandID::COMMAND_ID_REQUEST_ATTRIBUTES) {
            // Not a response header: a continuation chunk of a response that
            // was already discarded as stale. Drop it and keep waiting for the
            // expected response; the fetch timeout bounds the wait.
            LOG_DEBUG("Discarding stale chunk (0x%02X)\n", commandID);
            discardPending();
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
            discardPending();
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
                LOG_ERROR("Unknown attribute ID, or wrong order. Got 0x%02X, Expected 0x%02X\n",
                        attrID, mAttrs[mCurrentAttrIndex].id);
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
                    > mCurrentAttr->maxLen)
            {
                LOG_ERROR("Attribute length exceeds maxLen. Got 0x%02X, Expected 0x%02X\n",
                        mCurrentAttrLength, mCurrentAttr->maxLen);
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


} /* namespace CANS */

} /* namespace Ble */
