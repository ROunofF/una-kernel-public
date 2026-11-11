/**
 ******************************************************************************
 * @file    md5.cpp
 * @brief   MD5 (Message-Digest Algorithm 5) implementation.
 *
 * This implementation is derived from the RSA Data Security, Inc. MD5
 * Message-Digest Algorithm and modified slightly to be functionally identical
 * but condensed into control structures. RSA's notice for it (RFC 1321):
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All rights
 * reserved.
 *
 * License to copy and use this software is granted provided that it is
 * identified as the "RSA Data Security, Inc. MD5 Message-Digest Algorithm" in
 * all material mentioning or referencing this software or this function.
 *
 * License is also granted to make and use derivative works provided that such
 * works are identified as "derived from the RSA Data Security, Inc. MD5
 * Message-Digest Algorithm" in all material mentioning or referencing the
 * derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either the
 * merchantability of this software or the suitability of this software for
 * any particular purpose. It is provided "as is" without express or implied
 * warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * The class provides an incremental API:
 *  - init()
 *  - update()
 *  - finalize()
 *
 * and a one-shot helper:
 *  - calc()
 *
 ******************************************************************************
 *
 ******************************************************************************
*/

#include "MD5/md5.hpp"

#include <cstring>

/**
 * @name MD5 initial state constants
 * @{
 */
#define A 0x67452301
#define B 0xefcdab89
#define C 0x98badcfe
#define D 0x10325476
/** @} */

/**
 * @name Bit-manipulation functions defined by the MD5 algorithm
 * @{
 */
#define F(X, Y, Z) ((X & Y) | (~X & Z))
#define G(X, Y, Z) ((X & Z) | (Y & ~Z))
#define H(X, Y, Z) (X ^ Y ^ Z)
#define I(X, Y, Z) (Y ^ (X | ~Z))
/** @} */

/**
 * @brief Construct MD5 object and initialize internal context.
 */
MD5::MD5() : mContext{}
{
    init();
}

/**
 * @brief Initialize (or reset) MD5 context to the initial state.
 *
 * After calling init(), the object can be used to hash a new message.
 */
void MD5::init()
{
    mContext.size = (uint64_t)0;

    mContext.buffer[0] = (uint32_t)A;
    mContext.buffer[1] = (uint32_t)B;
    mContext.buffer[2] = (uint32_t)C;
    mContext.buffer[3] = (uint32_t)D;
}

/**
 * @brief Feed message bytes into MD5 state.
 *
 * This method can be called multiple times with consecutive chunks of data.
 * Internally it accumulates data until a full 64-byte block is available, then
 * processes it with the MD5 compression function.
 *
 * @param buff Pointer to input bytes (may be nullptr only if @p len is 0).
 * @param len  Number of bytes to process.
 */
void MD5::update(const uint8_t* buff, size_t len)
{
    uint32_t input[16];
    uint32_t offset = mContext.size % 64;
    mContext.size += (uint64_t)len;

    // Copy each byte in buf into the next space in our context input
    for (uint32_t i = 0; i < len; ++i) {
        mContext.input[offset++] = (uint8_t)*(buff + i);

        // If we've filled our context input, copy it into our local array input
        // then reset the offset to 0 and fill in a new buffer.
        // Every time we fill out a chunk, we run it through the algorithm
        // to enable some back and forth between cpu and i/o
        if (offset % 64 == 0) {
            for (uint32_t j = 0; j < 16; ++j) {
                // Convert 512-bit block (64 bytes) into 16 x 32-bit words.
                // Note: MD5 operates on little-endian words.
                input[j] = (uint32_t)(mContext.input[(j * 4) + 3]) << 24 |
                           (uint32_t)(mContext.input[(j * 4) + 2]) << 16 |
                           (uint32_t)(mContext.input[(j * 4) + 1]) <<  8 |
                           (uint32_t)(mContext.input[(j * 4)]);
            }

            step(mContext.buffer, input);

            offset = 0;
        }
    }
}

/**
 * @brief Finalize MD5 calculation and return the digest.
 *
 * This method:
 *  - Appends MD5 padding (0x80 followed by zeros)
 *  - Appends the original message length in bits (64-bit)
 *  - Processes the final block(s)
 *  - Produces a 16-byte digest
 *
 * @return MD5 digest (16 bytes).
 */
MD5::Digest MD5::finalize()
{
    uint32_t input[16];
    uint32_t offset = mContext.size % 64;
    uint32_t padding_length = offset < 56 ? 56 - offset : (56 + 64) - offset;

    // Append padding. update() increments size, so compensate afterwards to keep
    // mContext.size equal to the original message length (in bytes).
    update(PADDING, padding_length);
    mContext.size -= (uint64_t)padding_length;

    // Prepare final 512-bit block words (first 14 words from buffered data).
    // The last two 32-bit words hold message length in bits.
    for (uint32_t j = 0; j < 14; ++j) {
        input[j] = (uint32_t)(mContext.input[(j * 4) + 3]) << 24 |
                   (uint32_t)(mContext.input[(j * 4) + 2]) << 16 |
                   (uint32_t)(mContext.input[(j * 4) + 1]) <<  8 |
                   (uint32_t)(mContext.input[(j * 4)]);
    }

    input[14] = (uint32_t)(mContext.size * 8);
    input[15] = (uint32_t)((mContext.size * 8) >> 32);

    step(mContext.buffer, input);

    // Store digest as 16 bytes (little-endian output)
    for (uint32_t i = 0; i < 4; ++i) {
        mContext.digest.bytes[(i * 4) + 0] = (uint8_t)((mContext.buffer[i] & 0x000000FF));
        mContext.digest.bytes[(i * 4) + 1] = (uint8_t)((mContext.buffer[i] & 0x0000FF00) >>  8);
        mContext.digest.bytes[(i * 4) + 2] = (uint8_t)((mContext.buffer[i] & 0x00FF0000) >> 16);
        mContext.digest.bytes[(i * 4) + 3] = (uint8_t)((mContext.buffer[i] & 0xFF000000) >> 24);
    }

    return mContext.digest;
}

/**
 * @brief Calculate MD5 digest for a null-terminated C string (one-shot helper).
 *
 * Convenience helper that hashes the bytes of the string excluding the terminating '\\0'.
 *
 * @param str Null-terminated C string. If nullptr, calculates digest of empty string.
 * @return MD5 digest (16 bytes).
 */
MD5::Digest MD5::calc(const char* str)
{
    MD5 md5;

    if (str != nullptr) {
        md5.update((uint8_t*)str, std::strlen(str));
    }

    return md5.finalize();
}

/**
 * @brief Rotate a 32-bit word left by @p n bits.
 *
 * @param x Value to rotate.
 * @param n Number of bits.
 * @return Rotated value.
 */
uint32_t MD5::rotateLeft(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

/**
 * @brief Process one 512-bit block with the main MD5 compression function.
 *
 * Updates @p buffer in-place according to the MD5 transformation using
 * the provided 16-word block @p input.
 *
 * @param buffer Current MD5 state (4 x 32-bit words).
 * @param input  512-bit block split into 16 x 32-bit words.
 */
void MD5::step(uint32_t* buffer, const uint32_t* input)
{
    uint32_t AA = buffer[0];
    uint32_t BB = buffer[1];
    uint32_t CC = buffer[2];
    uint32_t DD = buffer[3];

    uint32_t E;
    uint32_t j;

    for (uint32_t i = 0; i < 64; ++i) {
        switch (i / 16) {
            case 0:
                E = F(BB, CC, DD);
                j = i;
                break;
            case 1:
                E = G(BB, CC, DD);
                j = ((i * 5) + 1) % 16;
                break;
            case 2:
                E = H(BB, CC, DD);
                j = ((i * 3) + 5) % 16;
                break;
            default:
                E = I(BB, CC, DD);
                j = (i * 7) % 16;
                break;
        }

        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + rotateLeft(AA + E + K[i] + input[j], S[i]);
        AA = temp;
    }

    buffer[0] += AA;
    buffer[1] += BB;
    buffer[2] += CC;
    buffer[3] += DD;
}
