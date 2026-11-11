/**
 ******************************************************************************
 * @file    AccessoryPayloadReader.hpp
 * @brief   Bounds-checked little-endian cursor over a GATT notification.
 ******************************************************************************
 *
 * CPS and CSC payloads are flag-gated: a leading Flags field says which optional
 * fields follow, so the parsers walk the buffer field by field and the length is
 * only known once the flags are decoded. Mis-stepping that walk -- reading a
 * field that is absent, or failing to skip one that is present -- is the single
 * most common cause of misparsed cycling BLE data, and it silently yields
 * plausible-looking garbage rather than an error.
 *
 * This cursor makes the walk safe by construction: every read is bounds-checked
 * against the remaining length, and the first overrun latches a sticky failure
 * so callers can walk the whole frame and test ok() once at the end instead of
 * checking after every field. Reads after a failure return 0 and never advance.
 *
 * All multi-byte GATT fields are little-endian.
 *
 ******************************************************************************
 */

#ifndef ACCESSORY_PAYLOAD_READER_HPP
#define ACCESSORY_PAYLOAD_READER_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   Sequential little-endian reader with a sticky overrun flag.
 */
class PayloadReader {
public:

    PayloadReader(const uint8_t *data, uint8_t len)
        : mData(data)
        , mLeft(data != nullptr ? len : 0)
        , mOk(data != nullptr)
    {
    }

    /**
     * @brief   'false' once any read or skip has run past the end of the buffer.
     */
    bool ok() const { return mOk; }

    /**
     * @brief   Bytes not yet consumed (0 once a read has overrun).
     */
    uint8_t remaining() const { return mOk ? mLeft : 0; }

    uint8_t  u8()  { return static_cast<uint8_t>(take(1)); }
    uint16_t u16() { return static_cast<uint16_t>(take(2)); }
    uint32_t u32() { return take(4); }

    /**
     * @brief   Read a signed 16-bit field (e.g. CPS instantaneous power, which
     *          is signed by spec -- trainers may report small negative values).
     */
    int16_t i16() { return static_cast<int16_t>(static_cast<uint16_t>(take(2))); }

    /**
     * @brief   Step over a present-but-unwanted field. Must be used for any
     *          optional field that PRECEDES one we care about, or every
     *          subsequent field is misread.
     */
    void skip(uint8_t bytes) { (void)take(bytes); }

private:

    /// Consume `bytes` little-endian, latching failure on overrun.
    uint32_t take(uint8_t bytes)
    {
        if (!mOk || mLeft < bytes) {
            mOk = false;
            return 0;
        }
        uint32_t value = 0;
        for (uint8_t i = 0; i < bytes; ++i) {
            value |= static_cast<uint32_t>(mData[i]) << (8u * i);
        }
        mData += bytes;
        mLeft = static_cast<uint8_t>(mLeft - bytes);
        return value;
    }

    const uint8_t *mData;
    uint8_t        mLeft;
    bool           mOk;
};

}  /* namespace Ble */

#endif /* ACCESSORY_PAYLOAD_READER_HPP */
