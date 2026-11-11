/**
 ******************************************************************************
 * @file    WordMove.hpp
 * @brief   Word-at-a-time copy and fill, for buffers too large for the libc.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_WORD_MOVE_HPP
#define COMPONENT_WORD_MOVE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * @brief   Moving bytes without going through the shared libc.
 *
 *          The kernel discards its own libc and resolves string functions
 *          from the fixed-address table apps link against, which is listed in
 *          the SDK's LibC/libc_exports_<version>.ld. Both memcpy and memset
 *          resolve there to plain byte loops, around five cycles a byte. They
 *          cannot be fixed where they live: the table's contents are the
 *          libcVersion ABI.
 *
 *          On anything the size of a framebuffer that is milliseconds. Moving
 *          whole words instead, unrolled by four so the compiler pairs them
 *          into ldrd/strd, came out roughly six times faster on a 57 600 byte
 *          frame -- one measurement on one build, indicative rather than a
 *          number to hold the compiler to.
 *
 *          Worth it only where the buffer is large and word-aligned. Where it
 *          is not, these fall back to the libc rather than pretending: the
 *          point is a fast path for the cases that have one, not a general
 *          replacement.
 */
namespace Tools {

// The buffers being moved are byte arrays, and reading them through a plain
// uint32_t* is undefined: alignment does not put uint32_t objects in byte
// storage, and -Os turns on -fstrict-aliasing. Saying the word type may alias
// anything is what makes the access defined, and it costs nothing -- the
// emitted loops are unchanged, ldrd/strd at both -Os and -O2.
//
// memcpy of a fixed-size word is the portable alternative and was measured
// instead: it is fine for the copy, but the compiler recognises a loop of
// stored zeros and turns the fill back into a call to the very memset this
// file exists to avoid. Not worth having two techniques for.
#if defined(__GNUC__)
using Word = uint32_t __attribute__((may_alias));
#else
using Word = uint32_t;
#endif

/**
 * @brief   Copy bytes, a word at a time where the pointers allow it.
 * @param   dst: destination.
 * @param   src: source, may overlap nothing -- this is a copy, not a move.
 * @param   bytes: how many. Any length; a trailing part-word is copied too.
 */
inline void CopyWordwise(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    constexpr size_t    kWord = sizeof(uint32_t);
    constexpr uintptr_t kMask = kWord - 1;

    // Either end unaligned and the word path would fault on this core, so the
    // libc takes it. Both ends are checked: a caller may own one and not the
    // other.
    if (((reinterpret_cast<uintptr_t>(dst) & kMask) != 0) ||
        ((reinterpret_cast<uintptr_t>(src) & kMask) != 0)) {
        memcpy(dst, src, bytes);
        return;
    }

    const Word *s = reinterpret_cast<const Word*>(src);
    Word       *d = reinterpret_cast<Word*>(dst);
    size_t      words = bytes / kWord;

    // Four at a time, because the compiler pairs these into ldrd/strd and that
    // is where most of the gain over a word-at-a-time loop comes from.
    while (words >= 4) {
        const uint32_t w0 = s[0];
        const uint32_t w1 = s[1];
        const uint32_t w2 = s[2];
        const uint32_t w3 = s[3];

        d[0] = w0;
        d[1] = w1;
        d[2] = w2;
        d[3] = w3;

        s += 4;
        d += 4;
        words -= 4;
    }

    while (words-- > 0) {
        *d++ = *s++;
    }

    size_t tail = bytes & kMask;
    if (tail == 0) {
        return;
    }

    const uint8_t *sb = reinterpret_cast<const uint8_t*>(s);
    uint8_t       *db = reinterpret_cast<uint8_t*>(d);

    while (tail-- > 0) {
        *db++ = *sb++;
    }
}

/**
 * @brief   Zero bytes, a word at a time where the pointer allows it.
 * @param   dst: destination.
 * @param   bytes: how many. Any length; a trailing part-word is cleared too.
 */
inline void ZeroWordwise(uint8_t *dst, size_t bytes)
{
    constexpr size_t    kWord = sizeof(uint32_t);
    constexpr uintptr_t kMask = kWord - 1;

    if ((reinterpret_cast<uintptr_t>(dst) & kMask) != 0) {
        memset(dst, 0, bytes);
        return;
    }

    Word   *d = reinterpret_cast<Word*>(dst);
    size_t  words = bytes / kWord;

    while (words >= 4) {
        d[0] = 0;
        d[1] = 0;
        d[2] = 0;
        d[3] = 0;

        d += 4;
        words -= 4;
    }

    while (words-- > 0) {
        *d++ = 0;
    }

    size_t   tail = bytes & kMask;
    uint8_t *db   = reinterpret_cast<uint8_t*>(d);

    while (tail-- > 0) {
        *db++ = 0;
    }
}

} // namespace Tools

#endif // COMPONENT_WORD_MOVE_HPP
