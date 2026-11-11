/**
 ******************************************************************************
 * @file    FakeMs5837.hpp
 * @brief   A scripted MS5837 on a fake I2C bus, for host tests of the driver
 *          and of what is built on it.
 ******************************************************************************
 */

#ifndef HOST_TEST_FAKE_MS5837_HPP
#define HOST_TEST_FAKE_MS5837_HPP

#include <cstdint>
#include <functional>

#include "Hardware/MS5837.hpp"

namespace FakeMs5837
{

// Datasheet product-version codes (PROM word0 bits [11:5]).
constexpr uint8_t kFactory02BA01  = 0x00;   // the board part - the only supported one
constexpr uint8_t kFactory02BA21  = 0x15;
constexpr uint8_t kFactory30BA26  = 0x1A;
constexpr uint8_t kFactoryUnknown = 0x7F;

/**
 * @brief   Build a PROM with a valid CRC nibble for the given factory code and
 *          coefficients: the CRC is computed over the image with the high
 *          nibble of word0 and word7 zeroed, then stored in word0's high
 *          nibble.
 */
inline Hardware::MS5837::PROM makeProm(uint8_t factory,
        uint16_t c1 = 40000, uint16_t c2 = 38000,
        uint16_t c3 = 25000, uint16_t c4 = 22000,
        uint16_t c5 = 30000, uint16_t c6 = 26000)
{
    Hardware::MS5837::PROM p {};
    p.raw[0] = static_cast<uint16_t>((factory & 0x7Fu) << 5);   // high nibble 0
    p.raw[1] = c1; p.raw[2] = c2; p.raw[3] = c3;
    p.raw[4] = c4; p.raw[5] = c5; p.raw[6] = c6;
    p.raw[7] = 0;

    const uint8_t crc = Hardware::MS5837::Crc4(p.raw);
    p.raw[0] = static_cast<uint16_t>(p.raw[0] | (crc << 12));
    return p;
}

/**
 * @brief   Fake I2C bus that scripts a MS5837: it serves a PROM image and a
 *          pair of D1/D2 ADC words, and can be told to fail. A convert command
 *          selects which ADC word the next ADC_READ returns.
 */
class Bus : public Interface::II2c
{
public:
    Hardware::MS5837::PROM prom {};
    uint32_t d1        = 0x900000;   // arbitrary plausible defaults
    uint32_t d2        = 0x800000;

    /// Added to each word once it has been converted, so that a part left
    /// alone returns the same pair for ever and one with a step does not.
    uint32_t d1Step    = 0;
    uint32_t d2Step    = 0;

    bool     ready     = true;       ///< Whether the address answers at all.
    bool     failAdc   = false;      ///< Whether an ADC read is refused.
    uint8_t  lastAddr  = 0xFF;       ///< The last address the driver used.

    /// Called as each conversion is started: what happens while the part
    /// converts, such as time passing.
    std::function<void()> onConversion;

    uint32_t resets      = 0;        ///< RESET commands taken.
    uint32_t conversions = 0;        ///< D1 conversions started.

    bool init() override                 { return true; }
    void deinit() override               {}
    bool isDeviceReady(uint8_t devAddr) override { lastAddr = devAddr; return ready; }
    bool isBusBusy() override            { return false; }
    void setCpltHandler(Interface::II2c::CpltHandler*) override {}

    bool receive(uint8_t, uint8_t*, uint16_t, bool = true) override { return false; }
    bool writeMem(uint8_t, uint16_t, uint8_t, const uint8_t*, uint16_t,
                  bool = true) override { return true; }

    bool transmit(uint8_t, const uint8_t* pData, uint16_t size,
                  bool = true) override
    {
        if (!ready) {
            return false;
        }

        if (size >= 1) {
            const uint8_t cmd = pData[0];
            if (cmd == Hardware::MS5837::Command::RESET) {
                resets++;
            } else if ((cmd & 0xF0u) == 0x40u) {    // CONVERT_D1_*
                mNextAdc = d1;
                d1 += d1Step;
                conversions++;
                converting();
            } else if ((cmd & 0xF0u) == 0x50u) {    // CONVERT_D2_*
                mNextAdc = d2;
                d2 += d2Step;
                converting();
            }
        }
        return true;
    }

    bool readMem(uint8_t, uint16_t memAddr, uint8_t, uint8_t* pData,
                 uint16_t size, bool = true) override
    {
        if (!ready) {
            return false;
        }

        if (memAddr == Hardware::MS5837::Command::ADC_READ && size == 3) {
            if (failAdc) {
                return false;
            }

            pData[0] = static_cast<uint8_t>(mNextAdc >> 16);
            pData[1] = static_cast<uint8_t>(mNextAdc >> 8);
            pData[2] = static_cast<uint8_t>(mNextAdc);
            return true;
        }
        // PROM word read: memAddr == 0xA0 + (index << 1).
        if (memAddr >= Hardware::MS5837::Command::PROM_READ_START && size == 2) {
            const uint16_t idx =
                    (memAddr - Hardware::MS5837::Command::PROM_READ_START) >> 1;
            if (idx < 8) {
                pData[0] = static_cast<uint8_t>(prom.raw[idx] >> 8);
                pData[1] = static_cast<uint8_t>(prom.raw[idx]);
                return true;
            }
        }
        return false;
    }

private:
    void converting()
    {
        if (onConversion) {
            onConversion();
        }
    }

    uint32_t mNextAdc = 0;
};

} // namespace FakeMs5837

#endif // HOST_TEST_FAKE_MS5837_HPP
