/**
 ******************************************************************************
 * @file    AccessoryAdvParser.hpp
 * @brief   Advertising-data parsing for accessory discovery.
 ******************************************************************************
 *
 * Walks the AD structures of a BLE advertising payload (or scan response) to
 * answer the two questions acquisition asks of every advertisement: does this
 * device offer a service we want, and what is it called.
 *
 * These were anonymous-namespace helpers inside AccessoryCentral.cpp and so had
 * no test coverage, despite parsing untrusted length-prefixed data straight off
 * the air. Lifting them here makes them host-testable and lets the scan match a
 * SET of service UUIDs rather than one, which is what multi-profile acquisition
 * needs: a scan must be able to find an HR strap, a power meter and a
 * speed sensor in the same burst and report which kind each hit was.
 *
 * PARSING RULES, all of which the tests pin down:
 *  - AD structures are [length][type][payload...], where `length` COUNTS the
 *    type byte. A zero length terminates the list (padding).
 *  - A length running past the end of the buffer means malformed data: stop,
 *    do not read past the end.
 *  - 16-bit service UUID lists are AD type 0x02 (incomplete) or 0x03 (complete);
 *    both must be honoured. A device may legitimately list several UUIDs.
 *  - Local name is AD type 0x08 (shortened) or 0x09 (complete).
 *
 * A device may advertise its service UUID in the advertisement, only in the scan
 * response, or not at all; straps differ.
 * These helpers decide only what a given payload contains; deciding what to do
 * about an absent UUID is the scanner's policy, not theirs.
 *
 ******************************************************************************
 */

#ifndef ACCESSORY_ADV_PARSER_HPP
#define ACCESSORY_ADV_PARSER_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   Does this advertising payload list any of the given 16-bit service
 *          UUIDs?
 * @param   data: advertising (or scan-response) AD structures.
 * @param   len: length of data in bytes.
 * @param   uuids: service UUIDs to look for.
 * @param   count: number of entries in uuids.
 * @param   matchedOut: optional; set to the UUID that matched. Untouched when
 *          the return is 'false', so the caller can key acquisition on WHICH
 *          service was seen (HRS vs CPS vs CSC) rather than merely that one was.
 * @retval  'true' if any listed UUID appears in a 16-bit service UUID list.
 */
bool advertisesAnyService(const uint8_t *data, uint8_t len,
        const uint16_t *uuids, uint8_t count, uint16_t *matchedOut = nullptr);

/**
 * @brief   Single-UUID convenience wrapper over advertisesAnyService().
 */
bool advertisesService(const uint8_t *data, uint8_t len, uint16_t serviceUuid);

/**
 * @brief   Copy the advertised local name into out, NUL-terminated.
 *
 * Leaves out empty when no name is advertised. Truncates to outSize - 1 rather
 * than overflowing: advertised names can be longer than the buffers that display
 * them, and a 24-byte buffer is smaller than the 29 bytes an AD structure can
 * carry.
 *
 * @param   data: advertising (or scan-response) AD structures.
 * @param   len: length of data in bytes.
 * @param   out: destination buffer.
 * @param   outSize: size of out in bytes; must be >= 1.
 */
void parseAdvName(const uint8_t *data, uint8_t len, char *out, uint8_t outSize);

}  /* namespace Ble */

#endif /* ACCESSORY_ADV_PARSER_HPP */
