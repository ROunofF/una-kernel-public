/**
 ******************************************************************************
 * @file    MS5837.cpp
 * @brief   Digital pressure and temperature sensor.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MS5837.hpp"

#include <cstddef>

#define LOG_MODULE_PRX      "MS5837"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "OS.hpp"

namespace Hardware
{

/**
 * @brief Default constructor.
 *
 * Creates an unconfigured sensor instance.
 */
MS5837::MS5837()
    : mIsConfigured(false)
    , mInitialised(false)
    , mAddr(skI2cAddr)
    , mI2C(nullptr)
    , mPROM()
{
}

/**
 * @brief Constructor with I2C interface.
 *
 * @param i2c      Reference to an II2c implementation.
 * @param devAddr  I2C 7-bit device address.
 */
MS5837::MS5837(Interface::II2c& i2c, uint8_t devAddr)
    : mIsConfigured(true)
    , mInitialised(false)
    , mAddr(devAddr)
    , mI2C(&i2c)
    , mPROM()
{
}

/**
 * @brief Configure chip with I2C driver.
 *
 * @param i2c      Reference to an II2c implementation.
 * @param devAddr  I2C 7-bit device address (unshifted).
 * @return true if configuration is stored successfully.
 */
bool MS5837::config(Interface::II2c &i2c, uint8_t devAddr)
{
    mI2C          = &i2c;
    mAddr         = devAddr;
    mIsConfigured = true;
    mInitialised  = false;   // a new bus must be re-init'd before get()

    return true;
}

/**
 * @brief Checks if chip is ready for communication.
 *
 * @return true if device acknowledges on I2C bus, false otherwise.
 */
bool MS5837::isReady()
{
    return mIsConfigured && mI2C->isDeviceReady(mAddr);
}

/**
 * @brief Initialize the sensor: reset and read PROM coefficients.
 *
 * @return true if initialization succeeds and CRC check passes.
 */
bool MS5837::init()
{
    // Any prior success is void until this attempt completes: a failure below
    // must leave get() refusing, not running on a stale/foreign mPROM.
    mInitialised = false;

    // Check if device is ready
    if (!isReady()) {
        LOG_ERROR("device is not ready\n");
        return false;
    }

    if(!reset()) {
        return false;
    }

    // Wait for reset to complete
    delay(10);

    if(!getPROM(mPROM)) {
        return false;
    }

    // Reject a part this driver cannot convert. getPROM() already verified the
    // CRC, so an unrecognised variant here is a genuinely wrong/foreign chip,
    // not a corrupt read - converting it with the wrong model would produce
    // plausible-looking but wrong values.
    if (!isSupportedModel(mPROM)) {
        LOG_ERROR("unsupported MS5837 model %s (PROM version 0x%02X)\n",
                  modelName(getModel(mPROM)), prom_factory(mPROM));
        return false;
    }

    LOG_INFO("MS5837 detected: %s (PROM version 0x%02X)\n",
             modelName(getModel(mPROM)), prom_factory(mPROM));

    mInitialised = true;
    return true;
}

/**
 * @brief Acquire compensated temperature and pressure.
 *
 * @param[out] temperature  Temperature in degrees Celsius.
 * @param[out] pressure     Pressure in mbar.
 * @return true if both conversions succeed and values are computed.
 */
bool MS5837::get(float &temperature, float &pressure)
{
    Raw raw;

    return get(temperature, pressure, raw);
}

/**
 * @brief Acquire compensated temperature and pressure, and the ADC words
 *        they were computed from.
 *
 * @param[out] temperature  Temperature in degrees Celsius.
 * @param[out] pressure     Pressure in mbar.
 * @param[out] raw          The two ADC words; untouched on failure.
 * @return true if both conversions succeed and values are computed.
 */
bool MS5837::get(float &temperature, float &pressure, Raw &raw)
{
    // Refuse until a successful init() has validated the part. Otherwise a
    // config()-only object converts an all-zero mPROM into a plausible
    // 20.00 C / 0.00 mbar, and a rejected foreign part would be read with the
    // wrong-model math this driver exists to prevent.
    if (!mInitialised) {
        LOG_ERROR("get() before a successful init()\n");
        return false;
    }

    uint32_t d1 = 0;
    uint32_t d2 = 0;

    if(!readD1(d1) || !readD2(d2)) {
        return false;
    }

    calculate(mPROM, d1, d2, temperature, pressure);

    raw.d1 = d1;
    raw.d2 = d2;

    LOG_DEBUG("d1       = %d\n", (int)d1);
    LOG_DEBUG("d2       = %d\n", (int)d2);
    LOG_DEBUG("mPROM.c0 = %d\n", (int)mPROM.c0);
    LOG_DEBUG("mPROM.c1 = %d\n", (int)mPROM.c1);
    LOG_DEBUG("mPROM.c2 = %d\n", (int)mPROM.c2);
    LOG_DEBUG("mPROM.c3 = %d\n", (int)mPROM.c3);
    LOG_DEBUG("mPROM.c4 = %d\n", (int)mPROM.c4);
    LOG_DEBUG("mPROM.c5 = %d\n", (int)mPROM.c5);
    LOG_DEBUG("mPROM.c6 = %d\n", (int)mPROM.c6);
    LOG_DEBUG("temperature = %f\n", temperature);
    LOG_DEBUG("pressure    = %f\n", pressure);

    return true;
}

/**
 * @brief Send RESET command to the sensor.
 *
 * @return true if I2C transmission succeeds.
 */
bool MS5837::reset()
{
    if (!mIsConfigured) {
        return false;
    }

    uint8_t cmd = Command::RESET;
    if (!mI2C->transmit(mAddr, &cmd, 1)) {
        LOG_ERROR("Failed to send command 0x%02X\n", cmd);
        return false;
    }

    return true;
}

/**
 * @brief Read PROM calibration data and verify CRC.
 *
 * @param[out] prom  PROM structure to fill.
 * @return true if PROM read and CRC match.
 */
bool MS5837::getPROM(PROM &prom)
{
    if (!mIsConfigured) {
        return false;
    }

    for (std::size_t i = 0; i < sizeof(prom.raw) / sizeof(prom.raw[0]) - 1; ++i) {
        uint16_t cmd = Command::PROM_READ_START + (static_cast<uint16_t>(i) << 1);
        uint8_t b[2];
        if (!mI2C->readMem(mAddr, cmd, 1, b, 2)) {
            LOG_ERROR("Failed to read PROM at 0x%02X\n", cmd);
            return false;
        }
        prom.raw[i] = (static_cast<uint16_t>(b[0]) << 8) | b[1]; // MSB first
    }

    const uint8_t crc_stored = (prom.raw[0] >> 12) & 0x0F; // high-nibble

    // Calculate CRC
    prom.raw[0] &= 0x0FFF;  // important: CRC in high nibble
    prom.raw[7] = 0;
    const uint8_t crc_calc = Crc4(prom.raw);

    if (crc_calc != crc_stored) {
        LOG_ERROR("Failed to verify CRC. stored=0x%X calc=0x%X (low=0x%X)\n",
                  crc_stored, crc_calc, uint8_t(prom.raw[0] & 0x0F));
        return false;
    }

    // CRC alone does not prove the PROM is real: an all-zero image passes it
    // trivially and reads as 02BA01. Reject it here too (see isDegeneratePROM).
    if (isDegeneratePROM(prom)) {
        LOG_ERROR("degenerate all-zero PROM\n");
        return false;
    }

    return true;
}

/**
 * @brief Validate D1 OSR command value.
 */
bool MS5837::IsOSRD1(uint8_t osr) {
    return (osr == CONVERT_D1_OSR_256 ||
            osr == CONVERT_D1_OSR_512 ||
            osr == CONVERT_D1_OSR_1024 ||
            osr == CONVERT_D1_OSR_2048 ||
            osr == CONVERT_D1_OSR_4096 ||
            osr == CONVERT_D1_OSR_8192);
}

/**
 * @brief Validate D2 OSR command value.
 */
bool MS5837::IsOSRD2(uint8_t osr) {
    return (osr == CONVERT_D2_OSR_256 ||
            osr == CONVERT_D2_OSR_512 ||
            osr == CONVERT_D2_OSR_1024 ||
            osr == CONVERT_D2_OSR_2048 ||
            osr == CONVERT_D2_OSR_4096 ||
            osr == CONVERT_D2_OSR_8192);
}

/**
 * @brief Extract the product-version code from PROM word C0 (bits [11:5]).
 *        Identifies the sensor variant per the MS5837 datasheet.
 */
uint8_t MS5837::prom_factory(const PROM& p)
{
    return static_cast<uint8_t>((p.raw[0] & 0x0FE0u) >> 5);
}

/**
 * @brief Identify the sensor variant from the PROM version code (datasheet
 *        values: 02BA01 = 0x00, 02BA21 = 0x15, 30BA26 = 0x1A).
 *
 * @return Model enum value, or UNRECOGNISED if the code is not known.
 */
MS5837::Model MS5837::getModel(const PROM &prom)
{
    const uint8_t f = prom_factory(prom);

    switch (f) {
        case 0x00: return Model::MS5837_02BA01;
        case 0x15: return Model::MS5837_02BA21;
        case 0x1A: return Model::MS5837_30BA26;
        default:   return Model::MS5837_UNRECOGNISED;
    }
}

const char* MS5837::modelName(Model m)
{
    switch (m) {
        case Model::MS5837_02BA01:       return "02BA01";
        case Model::MS5837_02BA21:       return "02BA21";
        case Model::MS5837_30BA26:       return "30BA26";
        case Model::MS5837_UNRECOGNISED: return "unrecognised";
    }
    return "unrecognised";
}

bool MS5837::isValidAdc(uint32_t raw)
{
    // 24-bit ADC. 0x000000 = conversion not started / not ready; 0xFFFFFF =
    // bus held high (no slave driving it); anything above 0xFFFFFF is outside
    // the 24-bit range entirely. None is a real measurement, so accept only the
    // open interval (0x000000, 0xFFFFFF).
    return raw > 0x000000u && raw < 0xFFFFFFu;
}

bool MS5837::isDegeneratePROM(const PROM& prom)
{
    // A dead or foreign device that ACKs but returns 0x0000 for every word
    // passes the CRC trivially (CRC of zero is zero) and reads as version
    // 0x00 = 02BA01. A genuine part never ships all-zero calibration.
    return (prom.c1 | prom.c2 | prom.c3 | prom.c4 | prom.c5 | prom.c6) == 0;
}

bool MS5837::isSupportedModel(const PROM& prom)
{
    // One answer to "is this a usable 02BA01" for every caller, including the
    // public-static path (bring-up, recovery, tests) that never runs through
    // getPROM()/init(). The board carries the MS5837-02BA01 only (MS583702BA01-50
    // on the BOM of every device); any other version code - even the 02BA21,
    // which shares the 02BA math - means a misdetected or foreign part and must
    // be rejected rather than converted. An all-zero image reads as 02BA01 but
    // is a dead/foreign part, so reject that here as well.
    return !isDegeneratePROM(prom) && getModel(prom) == Model::MS5837_02BA01;
}

/**
 * @brief Trigger and read raw D1 (pressure) ADC conversion.
 *
 * @param[out] d1     Raw ADC value for pressure.
 * @param[in]  d1osr  Conversion command with OSR bits.
 * @return true if conversion and read succeed.
 */
bool MS5837::readD1(uint32_t &d1, Command d1osr)
{
    if (!mIsConfigured) {
        return false;
    }

    if (!IsOSRD1(d1osr)) {
        return false;
    }

    const uint16_t cmd = Command::ADC_READ;
    uint8_t data[3] { };

    // Request D1 conversion
    if (!mI2C->transmit(mAddr, reinterpret_cast<const uint8_t*>(&d1osr), 1)) {
        LOG_ERROR("Failed to send command 0x%02X\n", d1osr);
        return false;
    }

    delay(25); // Max conversion time

    if (!mI2C->readMem(mAddr, cmd, 1, reinterpret_cast<uint8_t*>(&data[0]), 3)) {
        LOG_ERROR("Failed to read ADC. Command 0x%02X\n", cmd);
        return false;
    }

    d1 = (data[0] << 16) + (data[1] << 8) + data[2];

    if (!isValidAdc(d1)) {
        LOG_ERROR("invalid D1 ADC word 0x%06lX\n", (unsigned long)d1);
        return false;
    }

    return true;
}

/**
 * @brief Trigger and read raw D2 (temperature) ADC conversion.
 *
 * @param[out] d2     Raw ADC value for temperature.
 * @param[in]  d2osr  Conversion command with OSR bits.
 * @return true if conversion and read succeed.
 */
bool MS5837::readD2(uint32_t &d2, Command d2osr)
{
    if (!mIsConfigured) {
        return false;
    }

    if (!IsOSRD2(d2osr)) {
        return false;
    }

    const uint16_t cmd = Command::ADC_READ;
    uint8_t data[3] { };

    // Request D2 conversion
    if (!mI2C->transmit(mAddr, reinterpret_cast<const uint8_t*>(&d2osr), 1)) {
        LOG_ERROR("Failed to send command 0x%02X\n", d2osr);
        return false;
    }

    delay(25); // Max conversion time

    if (!mI2C->readMem(mAddr, cmd, 1, reinterpret_cast<uint8_t*>(&data[0]), 3)) {
        LOG_ERROR("Failed to read ADC. Command 0x%02X\n", cmd);
        return false;
    }

    d2 = (data[0] << 16) + (data[1] << 8) + data[2];

    if (!isValidAdc(d2)) {
        LOG_ERROR("invalid D2 ADC word 0x%06lX\n", (unsigned long)d2);
        return false;
    }

    return true;
}

/**
 * @brief Calculate compensated temperature and pressure.
 *
 * First- and second-order compensation per the MS5837-02BA datasheet. This
 * driver supports the 02BA variant only (init() rejects any other), so the
 * 30BA pressure scaling is intentionally not implemented.
 *
 * @param[in]  prom         Calibration coefficients from PROM.
 * @param[in]  d1           Raw pressure ADC reading.
 * @param[in]  d2           Raw temperature ADC reading.
 * @param[out] temperature  Compensated temperature in degrees Celsius.
 * @param[out] pressure     Compensated pressure in mbar.
 */
void MS5837::calculate(const PROM& prom,
                       uint32_t    d1,
                       uint32_t    d2,
                       float&      temperature,
                       float&      pressure)
{
    // Difference between actual and reference temperature.
    int64_t dT = static_cast<int64_t>(d2) - static_cast<int64_t>(prom.c5) * 256LL;

    // Actual temperature (-40..85 C with 0.01 C resolution)
    int64_t TEMP = 2000LL + (dT * static_cast<int64_t>(prom.c6)) / 8388608LL;

    // Temperature-compensated pressure offset and sensitivity (02BA scaling).
    int64_t OFF  = static_cast<int64_t>(prom.c2) * 131072 + (static_cast<int64_t>(prom.c4) * dT) / 64;
    int64_t SENS = static_cast<int64_t>(prom.c1) * 65536  + (static_cast<int64_t>(prom.c3) * dT) / 128;

    // Second-order compensation (02BA: low-temperature correction only).
    int64_t Ti    = 0;
    int64_t OFFi  = 0;
    int64_t SENSi = 0;
    if (TEMP < 2000) {
        Ti    = (11 * dT * dT) / 34359738368LL;
        OFFi  = (31 * (TEMP - 2000) * (TEMP - 2000)) / 8;
        SENSi = (63 * (TEMP - 2000) * (TEMP - 2000)) / 32;
    }

    // Calculate pressure and temp second order
    int64_t OFF2 = OFF - OFFi;
    int64_t SENS2 = SENS - SENSi;

    int64_t TEMP2 = (TEMP - Ti);    // C x 100
    int64_t P2 = ((static_cast<int64_t>(d1) * SENS2) / 2097152 - OFF2) / 32768;  // mbar x 100

    temperature = TEMP2 / 100.0f;   // C

    pressure = P2 / 100.0f;     // mbar

}

/**
 * @brief Blocking delay helper.
 *
 * @param ms  Milliseconds to wait.
 */
void MS5837::delay(uint32_t ms)
{
    OS::Delay(ms);
}

/**
 * @brief Compute 4-bit CRC from PROM coefficients.
 *
 * @param prom  Array of 8 PROM words (C0..C6).
 * @return 4-bit CRC value.
 */
uint8_t MS5837::Crc4(const uint16_t prom[8])
{
    uint16_t rem = 0;

    for (uint8_t i = 0; i < 16; ++i) {
        rem ^= (i & 1) ? (uint16_t)(prom[i >> 1] & 0x00FF)
                       : (uint16_t)(prom[i >> 1] >> 8);

        for (uint8_t b = 0; b < 8; ++b) {
            rem = (rem & 0x8000) ? (uint16_t)((rem << 1) ^ 0x3000)
                                 : (uint16_t)(rem << 1);
        }
    }

    return (uint8_t)((rem >> 12) & 0x0F);
}

} // namespace Hardware
