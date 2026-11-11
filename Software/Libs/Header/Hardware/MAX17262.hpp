/**
 ******************************************************************************
 * @file    MAX17262.hpp
 * @brief   1-Cell Fuel Gauge.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_MAX17262_HPP
#define HARDWARE_MAX17262_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

namespace Hardware
{

class MAX17262
{

public:
    enum class ProfileStatus {
        MATCHES,
        NO_MATCHES,
        UNKNOWN
    };

    /**
     * @brief   EZ-algorithm configuration. The application sets these
     *          project-specific values; the driver encodes them into the
     *          gauge's registers.
     */
    struct Config {
        uint16_t designCapMah;        ///< DesignCap (reg 0x18), mAh
        uint16_t termCurrentMa;       ///< IChgTerm (reg 0x1E), mA
        uint16_t emptyVoltageMv;      ///< VEmpty.VE (reg 0x3A bits 15:7), mV (10 mV LSB)
        uint16_t recoveryVoltageMv;   ///< VEmpty.VR (reg 0x3A bits 6:0), mV (40 mV LSB)
        uint16_t fullSocThrPct;       ///< FullSOCThr (reg 0x13), % (end-of-charge gate)
        uint16_t socHold;             ///< SOCHold (reg 0xD3), raw bitfield (99%HoldEn / Empty Hold)
        bool     highVoltageBattery;  ///< ModelCFG.VChg: true for >4.25 V cells
    };

    /**
     * @brief   Decoded telemetry snapshot, read in a single I2C burst.
     *          Capacities in mAh, SOC in %, voltages in V, currents in mA,
     *          temperature in degrees C. The set is the production-grade
     *          subset chosen for long-term in-field observation - enough to
     *          track cell health, energy, and the m5 learning state, without
     *          the derived/duplicated values that just bloat the log.
     */
    struct Telemetry {
        // Status / flags (raw bitfields)
        uint16_t status;      ///< Status (reg 0x00)
        uint16_t fStat;       ///< FStat  (reg 0x3D), DNR / learn flags
        uint16_t cycles;      ///< Cycles (reg 0x17)

        // m5 learning servo state (raw, hex-logged)
        uint16_t rComp0;      ///< RComp0 (reg 0x38), learned cell resistance
        uint16_t tempCo;      ///< TempCo (reg 0x39), learned RComp0 temperature coefficient
        uint16_t dQAcc;       ///< dQAcc  (reg 0x45), learning charge accumulator
        uint16_t dPAcc;       ///< dPAcc  (reg 0x46), learning percent accumulator

        // Electrical (decoded)
        float    vCell;       ///< VCell    (reg 0x09), V
        float    avgVCell;    ///< AvgVCell (reg 0x19), V
        float    temp;        ///< Temp (reg 0x08), degrees C. Reports the gauge
                              ///  die temperature when there is no external
                              ///  cell thermistor (the case for this hardware).
        float    current;     ///< Current    (reg 0x0A), mA, signed
        float    avgCurrent;  ///< AvgCurrent (reg 0x0B), mA, signed

        // SOC family (decoded)
        float    repSoc;      ///< RepSOC (reg 0x06), reported %

        // Capacity family (decoded)
        float    fullCapRep;  ///< FullCapRep (reg 0x10), load-adjusted, mAh
        float    fullCapNom;  ///< FullCapNom (reg 0x23), nominal cell capacity, mAh
        float    qResidual;   ///< QResidual  (reg 0x0C), reserved-below-empty, mAh
        float    qh;          ///< QH (reg 0x4D), running coulomb counter, mAh, signed
    };

    // I2C addresses
    static const uint8_t skI2cAddr = 0x36;

    /**
     * @brief   Constructor.
     */
    MAX17262() = default;

    /**
     * @brief   Constructor.
     * @param   pI2c: pointer to I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     */
    MAX17262(Interface::II2c *pI2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Destructor.
     */
    virtual ~MAX17262() = default;

    /**
     * @brief   Configure chip with I2C driver.
     * @param   pI2c: pointer to an I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c *pI2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Checks if chip is ready for communication.
     * @retval  Execution status. 'true' - device ready, 'false' - device
     *          not responded.
     */
    bool isReady();

    /**
     * @brief   Read register.
     * @param   reg: register to read.
     * @param   v: reference to save register value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readReg(uint8_t reg, uint16_t &v);

    /**
     * @brief   Write register.
     * @param   reg: register to write.
     * @param   v: new register value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeReg(uint8_t reg, uint16_t v);

    /**
     * @brief   Burst-read a contiguous block of 16-bit registers in one I2C
     *          transaction. The gauge auto-increments its register pointer.
     * @param   startReg: first register address.
     * @param   buf: destination, must hold regCount*2 bytes (little-endian).
     * @param   regCount: number of 16-bit registers to read.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readBlock(uint8_t startReg, uint8_t* buf, uint8_t regCount);

    /**
     * @brief   Read and decode a telemetry snapshot using one block read
     *          (regs 0x00..0x4F) plus one read for VFSOC. Minimizes wake
     *          time compared with reading each field separately.
     * @param   t: destination telemetry.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool readTelemetry(Telemetry& t);

    /**
     * @brief   Check if POR event has occurred.
     * @retval  'true' POR event has occurred, 'false' otherwise.
     */
    bool isPowerOnReset();

    /**
     * @brief   Reset POR event flag.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool clearPowerOnFlag();

    /**
     * @brief   Load the EZ-algorithm model from the configuration and reset
     *          the learned state to design defaults.
     * @note    Writes the config registers (DesignCap, IChgTerm, VEmpty,
     *          FullSOCThr, SOCHold) and runs the model Refresh sequence. Call
     *          when checkProfile() reports a mismatch or after a battery
     *          insertion. Does not clear the POR flag - the caller decides when.
     * @param   cfg: target configuration.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool loadModel(const Config& cfg);

    /**
     * @brief   Write only the DesignCap register, without a model Refresh.
     * @note    For rough SOC correction without a full model reload (e.g.
     *          the bootloader after a battery removal). The kernel should
     *          use loadModel() instead.
     * @param   mah: design capacity of the cell, mAh.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setDesignCap(uint16_t mah);

    /**
     * @brief   Check if battery detected.
     * @retval  'true' battery detected, 'false' otherwise.
     */
    bool isBattDetected();

    /**
     * @brief   Get SOC of the battery.
     * @retval  Actual SOC in % of maximum.
     */
    float getSoc();

    /**
     * @brief   Get actual capacity of the battery.
     * @retval  Actual capacity in mAh.
     */
    float getCapacity();

    /**
     * @brief   Get the gauge's learned full capacity (FullCapRep).
     * @retval  Full capacity in mAh.
     */
    float getFullCapacity();

    /**
     * @brief   Get actual voltage of the battery.
     * @retval  Actual voltage in Volts.
     */
    float getVoltage();

    /**
     * @brief   Get averaged battery voltage (AvgVCell, reg 0x19). Filtered by
     *          the gauge, so it is immune to the transient dips that a short
     *          load spike causes in the instantaneous VCell.
     * @retval  Averaged voltage in Volts.
     */
    float getAvgVoltage();

    /**
     * @brief   Get actual current which flows in (or out) the battery.
     * @retval  Actual current in mA. ( >0 - in, <0 - out )
     */
    float getCurrent();

    /**
     * @brief   Get average current which flows in (or out) the battery.
     * @retval  Average current in mA. ( >0 - in, <0 - out )
     */
    float getAvgCurrent();

    /**
     * @brief   Verify the gauge's EZ-config registers match a target.
     * @param   cfg: expected configuration.
     * @retval  MATCHES / NO_MATCHES / UNKNOWN.
     */
    ProfileStatus checkProfile(const Config& cfg);

private:
    struct ProfileItem {
        uint8_t  reg;
        uint16_t value;
    };

    // Indicates that the chip is configured
    bool mIsConfigured { };

    // Hardware configuration
    uint8_t mAddr { };
    Interface::II2c *mpI2c { };

    // MAX17262R has internal current sensing with FIXED register LSBs,
    // independent of the internal sense resistor (datasheet Table 2).
    static constexpr float    kSocScaler              = 1.0 / 256;
    static constexpr float    kCapScaler              = 0.5;            // 0.5 mAh/LSB
    static constexpr float    kVoltageScaler          = 78.125e-6;      // 78.125 uV/LSB
    static constexpr float    kCurrentScaler          = 0.15625;        // 156.25 uA/LSB
    static constexpr float    kTempScaler             = 1.0 / 256;      // degrees C / LSB (same LSB as SOC by coincidence)
    static constexpr uint32_t skI2cRetries            = 4;

    // Telemetry burst covers regs 0x00..0x4F (lowest 0x00 .. highest QH 0x4D).
    static constexpr uint8_t  skTelemetryBlockRegs    = 0x50;
    static constexpr uint16_t skTelemetryBlockBytes   = skTelemetryBlockRegs * 2;

    // ModelCFG register bit positions used by the EZ-config encoders.
    static constexpr uint16_t kModelCFG_RefreshRequest = 0x8000;
    static constexpr uint16_t kModelCFG_HighVoltage    = 0x0400;

    // EZ-config value encoders (mV / mA -> register code). The chip's
    // bit layout lives here; the values come from Config (application).
    static constexpr uint16_t encodeDesignCap(uint16_t mah)
    {
        return static_cast<uint16_t>(mah / kCapScaler);
    }
    static constexpr uint16_t encodeTermCurrent(uint16_t ma)
    {
        return static_cast<uint16_t>(ma / kCurrentScaler);
    }
    static constexpr uint16_t encodeVEmpty(uint16_t emptyMv, uint16_t recoveryMv)
    {
        const uint16_t ve = static_cast<uint16_t>((emptyMv / 10) << 7); // 10 mV LSB, bits 15:7
        const uint16_t vr = static_cast<uint16_t>(recoveryMv / 40);    // 40 mV LSB, bits 6:0
        return static_cast<uint16_t>((ve & 0xFF80) | (vr & 0x007F));
    }
    static constexpr uint16_t encodeModelCfg(bool highVoltage)
    {
        // Steady-state value (Refresh bit self-clears after load).
        return static_cast<uint16_t>(highVoltage ? kModelCFG_HighVoltage : 0);
    }
    static constexpr uint16_t encodeFullSocThr(uint16_t percent)
    {
        // High byte holds the integer percent; the low byte is the fixed
        // 0x05 pattern (register bits D2,D0). 80% -> 0x5005, 95% -> 0x5F05.
        return static_cast<uint16_t>((percent << 8) | 0x05);
    }

    /**
     * @brief   Return system tick in ms
     * @retval  Current HAL time base time stamp
     */
    uint32_t getTick();

    /**
     * @brief   Delay.
     * @param   ms: time in milliseconds.
     */
    void delay(uint32_t ms);

    ProfileStatus checkProfile(const ProfileItem* profile, uint8_t size);
    bool writeProfile(const ProfileItem* profile, uint8_t size);

    /**
     * @brief   Reset the gauge's learned state to design defaults.
     * @note    Called from loadModel() - i.e. only on a profile mismatch
     *          (config change). The previous learning was done under the old
     *          config and is no longer valid, so force a clean relearn. Clears
     *          FullCap/dQAcc/dPAcc/Cycles and the learned resistance model.
     * @param   designCapRaw: DesignCap in register units.
     */
    void resetLearnedState(uint16_t designCapRaw);

    // Power-on-reset defaults for the learned resistance model, restored on a
    // learning reset (the model Refresh keeps the old learned values).
    static constexpr uint16_t kRComp0Default = 0x0700;
    static constexpr uint16_t kTempCoDefault = 0x223E;
    static constexpr uint16_t kDPAccReset    = 0x0C80;  // paired with dQAcc = DesignCap/2
};

} /* namespace Hardware */

#endif // HARDWARE_MAX17262_HPP
