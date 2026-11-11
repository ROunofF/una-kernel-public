/**
 ******************************************************************************
 * @file    BMM350.hpp
 * @brief   BMM350 3-axis magnetometer driver.
 *
 *          Register-level protocol only, over the Bosch SensorAPI on
 *          Interface::II2c. No threads, no timers, no policy: a transfer is
 *          retried a few times because the bus is shared, but what a failed
 *          call means is the caller's. Calls return 'true' on success.
 *
 *          The part has no FIFO: normal mode overwrites the data registers at
 *          each ODR tick, so a reader has to keep up with the rate it selected.
 *          Forced mode converts once per request and returns to suspend.
 ******************************************************************************
 */

#ifndef HARDWARE_BMM350_HPP
#define HARDWARE_BMM350_HPP

#include "II2c.hpp"
#include "bmm350_defs.h"

#include <cstdint>

namespace Hardware
{

class BMM350
{

public:

    /// I2C address with ADSEL tied low.
    static constexpr uint8_t skI2cAddr = 0x14;
    /// Content of CHIP_ID (0x00) on a genuine BMM350.
    static constexpr uint8_t skChipId  = 0x33;

    /**
     * @brief   Output data rate in normal mode.
     * @note    Values match PMU_CMD_AGGR_SET.odr; the ladder stops at 400 Hz.
     */
    enum class Odr : uint8_t {
        ODR_400_HZ    = BMM350_ODR_400HZ,
        ODR_200_HZ    = BMM350_ODR_200HZ,
        ODR_100_HZ    = BMM350_ODR_100HZ,
        ODR_50_HZ     = BMM350_ODR_50HZ,
        ODR_25_HZ     = BMM350_ODR_25HZ,
        ODR_12_5_HZ   = BMM350_ODR_12_5HZ,
        ODR_6_25_HZ   = BMM350_ODR_6_25HZ,
        ODR_3_125_HZ  = BMM350_ODR_3_125HZ,
        ODR_1_5625_HZ = BMM350_ODR_1_5625HZ,
    };

    /**
     * @brief   Hardware averaging, i.e. samples folded into one output.
     * @note    Not available at every ODR; see clampAveraging().
     */
    enum class Averaging : uint8_t {
        AVG_1 = BMM350_AVG_NO_AVG,
        AVG_2 = BMM350_AVG_2,
        AVG_4 = BMM350_AVG_4,
        AVG_8 = BMM350_AVG_8,
    };

    /**
     * @brief   Power mode. SUSPEND is the state after boot; the forced modes
     *          convert once and drop back to suspend.
     */
    enum class PowerMode : uint8_t {
        SUSPEND     = BMM350_PMU_CMD_SUS,
        NORMAL      = BMM350_PMU_CMD_NM,
        FORCED      = BMM350_PMU_CMD_FM,

        /// Legal only above 25 Hz and not for the first trigger after init.
        FORCED_FAST = BMM350_PMU_CMD_FM_FAST,
    };

    /**
     * @brief   INT pin behaviour.
     */
    struct InterruptConfig {
        bool latched    = true;     ///< Latched until INT_STATUS is read.
        bool activeHigh = true;     ///< Idle low, asserted high.
        bool pushPull   = true;     ///< Push-pull rather than open drain.
    };

    /**
     * @brief   One compensated measurement.
     */
    struct Sample {
        float x           = 0.0f;   ///< Magnetic flux density, uT.
        float y           = 0.0f;   ///< Magnetic flux density, uT.
        float z           = 0.0f;   ///< Magnetic flux density, uT.
        float temperature = 0.0f;   ///< Die temperature, degrees Celsius.

        /// When the part generated this measurement, in counts of 39.0625 us
        /// on its own clock, read in the same burst as the data. Runs only
        /// while streaming and wraps at 24 bits; unrelated to the system time
        /// base.
        uint32_t sensorTime = 0;
    };

    BMM350();
    explicit BMM350(Interface::II2c& i2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Bind the bus and the device address. Must precede every other
     *          call. Touches no hardware.
     * @param   i2c: Bus the part sits on, already initialised.
     * @param   devAddr: 7-bit address, not shifted.
     */
    void configure(Interface::II2c& i2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Probe for the part without changing its state: address ACK
     *          followed by a CHIP_ID match.
     * @retval  'true' if a BMM350 answered, 'false' otherwise.
     */
    bool isReady();

    /**
     * @brief   Read CHIP_ID.
     */
    bool readChipId(uint8_t& chipId);

    /**
     * @brief   Bring the part up: soft reset, CHIP_ID check, OTP compensation
     *          download, magnetic reset, all three axes enabled.
     * @note    Blocks for the reset and settling delays the part mandates.
     * @note    Leaves the part suspended at the reset default of 100 Hz and
     *          AVG_2; a caller that cares calls setOdr().
     * @retval  'true' if a supported part initialised, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Check whether init() has succeeded.
     */
    bool isInitialized() const;

    /**
     * @brief   Soft-reset the part and re-run init().
     * @note    Drops ODR, averaging, power mode and interrupt configuration;
     *          the caller re-applies whatever it had.
     */
    bool reset();

    /**
     * @brief   Set output data rate and averaging.
     * @note    Averaging is clamped down where the ODR forbids it
     *          (clampAveraging()), and the reduction is logged. Nothing is
     *          cached.
     */
    bool setOdr(Odr odr, Averaging avg);

    /**
     * @brief   Set the power mode.
     * @note    Blocks for the part's own settling time, tens of milliseconds
     *          entering NORMAL. The SensorAPI reads the current mode itself,
     *          so nothing is cached here.
     */
    bool setPowerMode(PowerMode mode);

    /**
     * @brief   Configure the INT pin. Does not enable the interrupt.
     */
    bool configureInterrupt(const InterruptConfig& config);

    /**
     * @brief   Enable or disable the data-ready interrupt.
     */
    bool enableInterrupt(bool enable);

    /**
     * @brief   Read and clear the data-ready status. Reading it releases a
     *          latched INT pin.
     */
    bool getDataReady(bool& ready);

    /**
     * @brief   Read one compensated sample from the data registers.
     * @note    Does not check data-ready: in normal mode it returns whatever
     *          the last completed conversion produced, so a caller polling
     *          faster than the ODR sees repeats.
     */
    bool readSample(Sample& sample);

    /**
     * @brief   Trigger one forced-mode conversion and read the result.
     * @note    Blocks for the conversion (up to ~30 ms at AVG_8) and leaves
     *          the part in suspend mode. Plain forced mode, never FM_FAST,
     *          whose legality depends on the configured rate.
     */
    bool measureForced(Sample& sample);

    /**
     * @brief   Configure the on-chip I2C watchdog, which resets the part's bus
     *          interface when a transfer stalls.
     * @param   longTimeout: 'true' selects the longer of the two intervals.
     */
    bool setI2cWatchdog(bool enable, bool longTimeout);

    /**
     * @brief   Pick the slowest ODR whose period still fits inside the
     *          requested one.
     * @param   periodUs: Requested period between samples, microseconds.
     * @retval  ODR to program. Clamped to the 400 Hz ceiling and the
     *          1.5625 Hz floor.
     */
    static Odr periodUsToOdr(uint32_t periodUs);

    /**
     * @brief   Nominal period of an ODR, in microseconds.
     */
    static uint32_t odrToPeriodUs(Odr odr);

    /**
     * @brief   Reduce averaging to what the ODR allows: 400 Hz permits AVG_1
     *          only, 200 Hz up to AVG_2, 100 Hz up to AVG_4.
     */
    static Averaging clampAveraging(Odr odr, Averaging avg);

private:

    static BMM350_INTF_RET_TYPE i2cRead(uint8_t reg, uint8_t* pData,
            uint32_t len, void* pIntf);
    static BMM350_INTF_RET_TYPE i2cWrite(uint8_t reg, const uint8_t* pData,
            uint32_t len, void* pIntf);
    static void delayUs(uint32_t periodUs, void* pIntf);

    bool readRegs(uint8_t reg, uint8_t* pData, uint32_t len);
    bool writeRegs(uint8_t reg, const uint8_t* pData, uint32_t len);

    Interface::II2c* mpI2c         = nullptr;
    uint8_t          mAddr         = skI2cAddr;
    bool             mIsConfigured = false;
    bool             mIsInited     = false;

    bmm350_dev       mDev          = {};

    /// Retries for a single register transfer. The bus is shared, so a
    /// refusal can be contention; kept small because each refusal may cost
    /// the bus timeout on the calling thread.
    static constexpr uint32_t skI2cRetries = 3;

    /// Pause between attempts, so an immediate NAK does not burn every retry
    /// before the part can recover. Not paid after the last attempt.
    static constexpr uint32_t skRetryDelayUs = 2000;

};

} // namespace Hardware

#endif // HARDWARE_BMM350_HPP
