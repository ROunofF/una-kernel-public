/**
 ******************************************************************************
 * @file    MS5837.hpp
 * @brief   MS5837 digital pressure + temperature sensor driver.
 *
 *          Supports the MS5837-02BA01 variant only: init() reads and CRC-checks
 *          the PROM and rejects any part whose version code is not 02BA01
 *          (0x00), plus an all-zero (dead/foreign) PROM. The pressure
 *          compensation is 02BA-specific; the 30BA scaling is deliberately not
 *          implemented. The reset / PROM / CRC / D1-D2 conversion protocol is
 *          common to the family.
 ******************************************************************************
 */

#ifndef HARDWARE_MS5837_HPP
#define HARDWARE_MS5837_HPP

#include "II2c.hpp"

namespace Hardware
{

class MS5837 {

public:
    static const uint8_t skI2cAddr = 0x76;      // I2C addresses is 1110110x (write: x=0, read: x=1)

    // Datasheet product-version codes, for identification and diagnostics only.
    // This driver converts the 02BA01 alone (see isSupportedModel); the other
    // codes exist so a misdetected part can be named in the reject log - they do
    // NOT imply support. Do not add the 30BA pressure scaling on the strength of
    // MS5837_30BA26 being listed here.
    enum Model : uint8_t {
        MS5837_02BA01 = 0x00,
        MS5837_02BA21 = 0x15,
        MS5837_30BA26 = 0x1A,
        MS5837_UNRECOGNISED = 255,
    };

    enum Command : uint8_t {
        RESET               = 0x1E,
        CONVERT_D1_OSR_256  = 0x40,
        CONVERT_D1_OSR_512  = 0x42,
        CONVERT_D1_OSR_1024 = 0x44,
        CONVERT_D1_OSR_2048 = 0x46,
        CONVERT_D1_OSR_4096 = 0x48,
        CONVERT_D1_OSR_8192 = 0x4A,
        CONVERT_D2_OSR_256  = 0x50,
        CONVERT_D2_OSR_512  = 0x52,
        CONVERT_D2_OSR_1024 = 0x54,
        CONVERT_D2_OSR_2048 = 0x56,
        CONVERT_D2_OSR_4096 = 0x58,
        CONVERT_D2_OSR_8192 = 0x5A,
        ADC_READ            = 0x00,
        PROM_READ_START     = 0xA0,
    };

#pragma pack(push, 1)
    union PROM {
        struct {
            uint16_t c0;
            uint16_t c1;
            uint16_t c2;
            uint16_t c3;
            uint16_t c4;
            uint16_t c5;
            uint16_t c6;
            uint16_t c7_reserved;
        };
        uint16_t raw[8];
    };
#pragma pack(pop)

    /// The two ADC words one measurement is computed from. On a part that
    /// is converting the low bits of both carry noise, so the same pair
    /// comes twice running only now and then, and a run of them marks a
    /// part that has stopped converting.
    struct Raw {
        uint32_t d1 = 0;    ///< Pressure conversion.
        uint32_t d2 = 0;    ///< Temperature conversion.
    };

    MS5837();
    MS5837(Interface::II2c &i2c, uint8_t devAddr = skI2cAddr);
    virtual ~MS5837() = default;

    bool  config(Interface::II2c &i2c, uint8_t devAddr = skI2cAddr);
    bool  isReady();
    bool  init();
    bool  get(float &temperature, float &pressure);
    bool  get(float &temperature, float &pressure, Raw &raw);
    bool  reset();

    // Pure, hardware-independent helpers. Static so host tests can drive them
    // directly without an I2C bus or OS.
    static Model getModel(const PROM &prom);
    static void  calculate(const PROM& prom,
                           uint32_t    d1,
                           uint32_t    d2,
                           float&      temperature,
                           float&      pressure);

    /// Reject an ADC word the device never produces: all-zero (conversion not
    /// started / not ready) or all-ones (bus stuck high). Either would feed the
    /// compensation a wild value that still looks valid downstream.
    static bool  isValidAdc(uint32_t raw);

    /// Whether the PROM is a usable board part: a genuine (non-degenerate)
    /// image whose version code is the 02BA01 this driver converts. The single
    /// answer to "can calculate() be trusted on this PROM", for every caller.
    static bool  isSupportedModel(const PROM& prom);

    /// 4-bit PROM checksum (datasheet algorithm). Public so tests can build a
    /// CRC-valid PROM image and exercise the integrity gate.
    static uint8_t Crc4(const uint16_t n_prom[8]);

private:
    static bool    IsOSRD1(uint8_t osr);
    static bool    IsOSRD2(uint8_t osr);
    static uint8_t prom_factory(const PROM& p);

    /// Human-readable variant name for a Model, for diagnostic logs only.
    static const char* modelName(Model m);

    /// True for an all-zero (dead/foreign) calibration image, which passes the
    /// CRC trivially and reads as version 0x00. Shared by getPROM() and
    /// isSupportedModel() so the degenerate case is rejected on every path.
    static bool    isDegeneratePROM(const PROM& prom);

    void delay(uint32_t ms);
    bool getPROM(PROM &prom);
    bool readD1(uint32_t &d1, Command d1osr = CONVERT_D1_OSR_8192);
    bool readD2(uint32_t &d2, Command d2osr = CONVERT_D2_OSR_8192);

    bool             mIsConfigured;
    bool             mInitialised;   // set only by a successful init()
    uint8_t          mAddr;
    Interface::II2c* mI2C;
    PROM             mPROM;
};

} // namespace Hardware

#endif // HARDWARE_MS5837_HPP
