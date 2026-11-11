/**
 ******************************************************************************
 * @file    PCA9420.hpp
 * @brief   Power Management IC (PMIC).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __HARDWARE_PCA9420_HPP
#define __HARDWARE_PCA9420_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

namespace Hardware
{

class PCA9420
{

public:

    // I2C addresses
    static const uint8_t skI2cAddr = 0x61;

    /**
     * @brief   Registers
     */
    class Reg {
    public:
        typedef enum : uint8_t {
            // System Control Registers
            DEV_INFO = 0x00,
            TOP_INT = 0x01,
            SUB_INT0 = 0x02,
            SUB_INT0_MASK = 0x03,
            SUB_INT1 = 0x04,
            SUB_INT1_MASK = 0x05,
            SUB_INT2 = 0x06,
            SUB_INT2_MASK = 0x07,

            TOP_CNTL0 = 0x09,
            TOP_CNTL1 = 0x0A,
            TOP_CNTL2 = 0x0B,
            TOP_CNTL3 = 0x0C,
            TOP_CNTL4 = 0x0D,

            // Battery Charger Control
            CHG_CNTL0 = 0x10,
            CHG_CNTL1 = 0x11,
            CHG_CNTL2 = 0x12,
            CHG_CNTL3 = 0x13,
            CHG_CNTL4 = 0x14,
            CHG_CNTL5 = 0x15,
            CHG_CNTL6 = 0x16,
            CHG_CNTL7 = 0x17,
            CHG_STATUS_0 = 0x18,
            CHG_STATUS_1 = 0x19,
            CHG_STATUS_2 = 0x1A,
            CHG_STATUS_3 = 0x1B,

            // Regulator Control
            REG_STATUS = 0x20,
            ACT_DISCHARGE_CNTL_1 = 0x21,
            MODECFG_0_0 = 0x22,
            MODECFG_0_1 = 0x23,
            MODECFG_0_2 = 0x24,
            MODECFG_0_3 = 0x25,
            MODECFG_1_0 = 0x26,
            MODECFG_1_1 = 0x27,
            MODECFG_1_2 = 0x28,
            MODECFG_1_3 = 0x29,
            MODECFG_2_0 = 0x2A,
            MODECFG_2_1 = 0x2B,
            MODECFG_2_2 = 0x2C,
            MODECFG_2_3 = 0x2D,
            MODECFG_3_0 = 0x2E,
            MODECFG_3_1 = 0x2F,
            MODECFG_3_2 = 0x30,
            MODECFG_3_3 = 0x31,
            RESET_MON = 0x71,
        } Id_t;

#pragma pack(push, 1)

        // 00
        union DevInfo {
            struct {
                uint8_t rev :3;
                uint8_t id :5;
            };
            uint8_t reg;
        };

        // 01
        union TopInt {
            struct {
                uint8_t ldo :1;
                uint8_t sw :1;
                uint8_t chg :1;
                uint8_t sys :1;
                uint8_t :4;
            };
            uint8_t reg;
        };

        // 02, 03
        union SubInt0 {
            struct {
                uint8_t vin :1;
                uint8_t wdTimer :1;
                uint8_t asysPrewarning :1;
                uint8_t themSdn :1;
                uint8_t tempPrewarning :1;
                uint8_t onPush :1;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 04, 05
        union SubInt1 {
            struct {
                uint8_t chgOk :1;
                uint8_t vbat :1;
                uint8_t batDetection :1;
                uint8_t iChgPreqTim :1;
                uint8_t iChgFastTim :1;
                uint8_t vInILim :1;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 06, 07
        union SubInt2 {
            struct {
                uint8_t voutLdo2 :1;
                uint8_t voutLdo1 :1;
                uint8_t voutSw2 :1;
                uint8_t voutSw1 :1;
                uint8_t :4;
            };
            uint8_t reg;
        };

        // 09
        union TopCntl0 {
            struct {
                uint8_t pgEn :1;
                uint8_t :1;
                uint8_t chgInWdEn :1;
                uint8_t pwrDnEn :1;
                uint8_t opSelFromSlpMode :1;
                uint8_t vinIlimSel :3;
            };
            uint8_t reg;
        };

        // 0A
        union TopCntl1 {
            struct {
                uint8_t vinUvLockoutSel :2;
                uint8_t vinOvpSel :1;
                uint8_t :1;
                uint8_t asysInSel :2;
                uint8_t asysPrewarning :2;
            };
            uint8_t reg;
        };

        // 0B
        union TopCntl2 {
            struct {
                uint8_t dieTempWarning :2;
                uint8_t thermShdn :3;
                uint8_t termDis :1;
                uint8_t asysUvLockoutSel :2;
            };
            uint8_t reg;
        };

        // 0C
        union TopCntl3 {
            struct {
                uint8_t onGltLon :2;
                uint8_t swReset :1;
                uint8_t mode0I2c :1;
                uint8_t mode1I2c :1;
                uint8_t :3;
            };
            uint8_t reg;
        };

        // 0D
        union TopCntl4 {
            struct {
                uint8_t wdClr :3;
                uint8_t :5;
            };
            uint8_t reg;
        };

        // 10
        union ChgCntl0 {
            struct {
                uint8_t chgEn :1;
                uint8_t chgTimEn :1;
                uint8_t ntcEn :1;
                uint8_t chgLock :5;
            };
            uint8_t reg;
        };

        // 11
        union ChgCntl1 {
            struct {
                uint8_t iFastChg :6;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 12
        union ChgCntl2 {
            struct {
                uint8_t iTapOffChg :6;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 13
        union ChgCntl3 {
            struct {
                uint8_t iPreChg :6;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 14
        union ChgCntl4 {
            struct {
                uint8_t iDeadChg :6;
                uint8_t :2;
            };
            uint8_t reg;
        };

        // 15
        union ChgCntl5 {
            struct {
                uint8_t vBatReg :6;
                uint8_t vBatRestart :1;
                uint8_t :1;
            };
            uint8_t reg;
        };

        // 16
        union ChgCntl6 {
            struct {
                uint8_t timTopOff :2;
                uint8_t iChgPreqTim :2;
                uint8_t iChgFastTim :2;
                uint8_t tim2x :1;
                uint8_t ntcResSel :1;
            };
            uint8_t reg;
        };

        // 17
        union ChgCntl7 {
            struct {
                uint8_t thmRegTrheshold :3;
                uint8_t :2;
                uint8_t ntcBetaSel :3;
            };
            uint8_t reg;
        };

        // 18
        union ChgStatus0 {
            struct {
                uint8_t :4;
                uint8_t chgOk :1;
                uint8_t vinOk :1;
                uint8_t vbatOk :1;
                uint8_t vbatDetOk :1;
            };
            uint8_t reg;
        };

        // 19
        union ChgStatus1 {
            struct {
                uint8_t :4;
                uint8_t tRegStatus :1;
                uint8_t :1;
                uint8_t vinStatus :2;
            };
            uint8_t reg;
        };

        // 1A
        union ChgStatus2 {
            struct {
                uint8_t chgStatus :3;
                uint8_t :1;
                uint8_t batDetailStatus :3;
                uint8_t :1;
            };
            uint8_t reg;
        };

        // 1B
        union ChgStatus3 {
            struct {
                uint8_t saftyTimerStatus :2;
                uint8_t chipTempStatus :1;
                uint8_t :1;
                uint8_t tempDetailStatus :3;
                uint8_t tempIsAbnormal :1;
            };
            uint8_t reg;
        };

        // 20
        union RegStatus {
            struct {
                uint8_t :4;
                uint8_t vLdo2Ok :1;
                uint8_t vLdo1Ok :1;
                uint8_t vSw2Ok :1;
                uint8_t vSw1Ok :1;
            };
            uint8_t reg;
        };

        // 21
        union ActDischargeCntl1 {
            struct {
                uint8_t nLdo2BleedEn :1;
                uint8_t nLdo1BleedEn :1;
                uint8_t nSw2BleedEn :1;
                uint8_t nSw1BleedEn :1;
                uint8_t :4;
            };
            uint8_t reg;
        };

        // 22, 26, 2A, 2E
        union Modecfgx0 {
            struct {
                uint8_t sw1Out :6;
                uint8_t selModeByPinEn :1;
                uint8_t shipModeEn :1;
            };
            uint8_t reg;
        };

        // 23, 27, 2B, 2F
        union Modecfgx1 {
            struct {
                uint8_t sw2OutLsb :5;
                uint8_t sw2OutOffset :1;
                uint8_t onCfg :1;
                uint8_t :1;
            };
            uint8_t reg;
        };

        // 24, 28, 2C, 30
        union Modecfgx2 {
            struct {
                uint8_t ldo2En :1;
                uint8_t ldo1En :1;
                uint8_t sw2En :1;
                uint8_t sw1En :1;
                uint8_t ldo1Out :4;
            };
            uint8_t reg;
        };

        // 25, 29, 2D, 31
        union Modecfgx3 {
            struct {
                uint8_t ldo2OutLsb :5;
                uint8_t ldo2OutOffset :1;
                uint8_t wdTimer :2;
            };
            uint8_t reg;
        };

        // 71
        union ResetMon {
            struct {
                uint8_t longOnDone :1;
                uint8_t wdRstDone :1;
                uint8_t swRstDone :1;
                uint8_t shipExitDone :1;
                uint8_t thmStdnDone :1;
                uint8_t :2;             // bits 6:5 reserved
                uint8_t onFiltered :1;  // bit 7
            };
            uint8_t reg;
        };

#pragma pack(pop)
    };

    /**
     * @brief   PMIC Mode.
     */
    enum class Mode : uint8_t {
        MODE_0 = 0,
        MODE_1,
        MODE_2,
        MODE_3,
    };

    /**
     * @brief   PMIC out.
     */
    enum class Out : uint8_t {
        SW1 = 0,
        SW2,
        LDO1,
        LDO2,
    };

    /**
     * @brief   Long glitch timer on ON key.
     */
    enum class LongGlitchON : uint8_t {
        TIME_4S = 0,
        TIME_8S,
        TIME_12S,
        TIME_16S,
    };

    /**
     * @brief   Interrupt mask structure
     */
    typedef struct {
        Reg::TopInt top;
        struct Mask {
            Reg::SubInt0 int0;
            Reg::SubInt1 int1;
            Reg::SubInt2 int2;
        } mask;
    } Irq_t;

    /**
     * @brief   Under-voltage lockout threshold (falling) on VIN.
     */
    enum class VinUVLO : uint8_t {
        VIN_UVLO_2900 = 0,
        VIN_UVLO_3100 = 1,
        VIN_UVLO_3300 = 2,
        VIN_UVLO_3500 = 3,
    };

    /**
     * @brief   ASYS input source selection.
     */
    enum class AsysInput : uint8_t {
        ASYS_INPUT_VBAT_VIN = 0,
        ASYS_INPUT_VBAT     = 1,
        ASYS_INPUT_VIN      = 2,
        ASYS_INPUT_NONE     = 3,
    };

    /**
     * @brief   VIN input current limit selection
     *
     * VIN input current limit selection: (min / typ / max)
     * 000:  74 mA /   85 mA /   98 mA   (another default setting by MTP)
     * 001: 222 mA /  255 mA /  293 mA
     * 010: 370 mA /  425 mA /  489 mA   (default setting)
     * 011: 517 mA /  595 mA /  684 mA
     * 100: 665 mA /  765 mA /  880 mA
     * 101: 813 mA /  935 mA / 1075 mA
     * 110: 961 mA / 1105 mA / 1271 mA
     * 111: Input current limit disabled
     *
     * Note: VIN_ILIM_SEL[1] is the only bit that is MTP configurable
     */
    enum class VinILim : uint8_t {
        VIN_LIM_85      = 0, ///<  74 /   85 /   98 mA
        VIN_LIM_255     = 1, ///< 222 /  255 /  293 mA
        VIN_LIM_425     = 2, ///< 370 /  425 /  489 mA (default)
        VIN_LIM_595     = 3, ///< 517 /  595 /  684 mA
        VIN_LIM_765     = 4, ///< 665 /  765 /  880 mA
        VIN_LIM_935     = 5, ///< 813 /  935 / 1075 mA
        VIN_LIM_1105    = 6, ///< 961 / 1105 / 1271 mA
        VIN_LIM_DISABLE = 7  ///< Input current limit disabled
    };

    /**
     * @brief   Die temperature warning threshold.
     */
    enum class TempWarning : uint8_t {
        TEMP_WARN_75 = 0,
        TEMP_WARN_80 = 1,
        TEMP_WARN_85 = 2,
        TEMP_WARN_90 = 3,
    };

    /**
     * @brief   Top-off timer duration.
     *          Time the charger stays in top-off mode before entering DONE.
     */
    enum class TopOffTimer : uint8_t {
        DISABLED    = 0,    ///< Top-off timer disabled (immediate DONE)
        TIME_6_4MIN = 1,    ///< 6.4 minutes (default)
        TIME_12_8MIN = 2,   ///< 12.8 minutes
        TIME_19_2MIN = 3,   ///< 19.2 minutes
    };

    /**
     * @brief   Fast charge safety timer duration.
     */
    enum class FastChargeTimer : uint8_t {
        TIME_3H = 0,    ///< 3 hours
        TIME_5H = 1,    ///< 5 hours (default)
        TIME_7H = 2,    ///< 7 hours
        TIME_9H = 3,    ///< 9 hours
    };

    /**
     * @brief   Charger status codes from CHG_STATUS_2 register.
     */
    enum class ChargeStatus : uint8_t {
        IDLE         = 0,
        DEAD_BATTERY = 1,
        LOW_BATTERY  = 2,
        FAST_CHARGE  = 3,   ///< CC or CV
        FAST_CHARGE2 = 4,   ///< CC or CV
        TOP_OFF      = 5,
        DONE         = 6,
        RESERVED     = 7,
    };

    /**
     * @brief   Constructor.
     */
    PCA9420() = default;

    /**
     * @brief   Constructor.
     * @param   pI2c: pointer to I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     */
    PCA9420(Interface::II2c *pI2c, uint8_t devAddr = skI2cAddr);

    /**
     * @brief   Destructor.
     */
    virtual ~PCA9420() = default;

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
    bool readReg(uint8_t reg, uint8_t &v);

    /**
     * @brief   Write register.
     * @param   reg: register to read.
     * @param   v: new register value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool writeReg(uint8_t reg, uint8_t v);

    /**
     * @brief   Software PMIC reset.
     * @brief   It resets all other I2C register bit to its default setting
     *          and cycle the regulator outputs.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool reset();

    /**
     * @brief   Enter SHIP mode, the lowest-power state.
     * @note    Triggers a power-down sequence, then parks the chip in ship
     *          mode. In ship mode attaching a battery does NOT power the
     *          chip up; only a valid VIN or an ON-key falling edge does.
     *          All I2C registers reset to defaults on ship mode entry.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool enterShipMode();

    /**
     * @brief   Enable/Disable mode selection by MODESEL0/MODESEL1 pins.
     * @param   mode: target mode.
     * @param   enable: state. 'true' - selection by pins, 'false' - by I2C.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setModeSelectionByPin(PCA9420::Mode mode, bool enable);

    /**
     * @brief   Enable/Disable switching to mode0 upon a valid falling edge
     *          detected from "ON" pin.
     * @param   mode: target mode.
     * @param   enable: state. 'true' - enable switching, 'false' - disable.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setSwitchBackToMode0(PCA9420::Mode mode, bool enable);

    /**
     * @brief   Set voltage.
     * @param   out: target out.
     * @param   mode: target mode.
     * @param   mV: target voltage. Step 25 mV.
     *          For SW1: Range 500 mV - 1500 mV.
     *          For SW2: Range 1500 mV - 2100 mV, 2700 mV - 3300 mV.
     *          For LDO1: Range: 1700 mV - 1900 mV.
     *          For LDO2: Range 1500 mV - 2100 mV, 2700 mV - 3300 mV.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setVoltage(PCA9420::Out out, PCA9420::Mode mode, uint32_t mV);

    /**
     * @brief   Enable/Disable out.
     * @param   out: target out.
     * @param   mode: target mode.
     * @param   isOn: target state 'true' - ON, 'false' - OFF.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool enableOut(PCA9420::Out out, PCA9420::Mode mode, bool isOn);

    /**
     * @brief   Set mode.
     * @param   mode: target mode.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setMode(PCA9420::Mode mode);

    /**
     * @brief   Set charge current limit.
     * @param   mA: current limit. Max 315 mA.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setChargeCurrentLim(uint16_t mA);

    /**
     * @brief   Enable/Disable TS pin external thermistor (NTC) control in
     *          charger.
     * @param   enable: state. 'true' - enable NTC, 'false' - disable.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setNtc(bool enable);

    /**
     * @brief   Select the external NTC thermistor resistance.
     * @param   sel: NTC_RES_SEL bit. 'false' - 100k thermistor,
     *          'true' - 10k thermistor.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setNtcResSel(bool sel);

    /**
     * @brief   Set Long glitch timer on ON key.
     * @param   time: LongGlitchON enum.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setLongGlitchOnTime(PCA9420::LongGlitchON time);

    /**
     * @brief Programs a battery regulation voltage
     *
     * @param value Range from 3600mV to 4600mV
     * @return Execution status. 'true' if success, 'false' otherwise.
     */
    bool setVBAT(uint16_t value);

    /**
     * @brief Reads current battery regulation voltage
     *
     * @param value Reference to value
     * @return Execution status. 'true' if success, 'false' otherwise.
     */
    bool getVBAT(uint16_t& value);

    /**
     * @brief   Check if Vin voltage detected.
     * @retval  'true' Vin detected, 'false' otherwise.
     */
    bool isVinDetected();

    /**
     * @brief   Check if Vbat voltage detected.
     * @note    Battery detection sequence is not automatically triggered
     *          when PMIC is powered only from battery (VBAT). You must
     *          initiate it manually via I2C.
     * @retval  'true' Vbat detected, 'false' otherwise.
     */
    bool isVbatDetected();

    /**
     * @brief   Check if battery is charging.
     * @retval  'true' battery is charging, 'false' otherwise.
     */
    bool isCharging();

    /**
     * @brief   Enable interrupts.
     * @param   mask: '1' - enable interrupt. '0' - unchanged.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool enableIrq(Irq_t::Mask mask);

    /**
     * @brief   Disable interrupts.
     * @param   mask: '1' - disable interrupt. '0' - unchanged.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool disableIrq(Irq_t::Mask mask);

    /**
     * @brief   Set interrupt mask to exact state: enable specified bits,
     *          disable all others.
     * @param   mask: '1' - enable interrupt, '0' - disable interrupt.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setIrq(Irq_t::Mask mask);

    /**
     * @brief   Get IRQ flags.
     * @retval  Active IRQ flags. '1' - irq has happened.
     *          '0' - irq has not happened.
     */
    Irq_t getIrqFlags();

    /**
     * @brief   Clear IRQ flags.
     * @param   mask: '1' - clear interrupt. '0' - unchanged.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool clearIrqFlags(Irq_t::Mask mask);

    /**
     * @brief   Program an under-voltage lockout threshold (falling) on VIN
     * @param   threshold: Under-voltage lockout threshold (2900, 3100, 3300, 3500).
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setVinUnderVoltageLockout(PCA9420::VinUVLO threshold);

    /**
     * @brief   ASYS input source selection
     * @param   source: ASYS input source.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setAsysInputSource(PCA9420::AsysInput source);

    /**
     * @brief   VIN input current limit selection
     * @param   lim: VIN input current limit.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setVinILim(PCA9420::VinILim lim);

    /**
     * @brief   Program a Die temperature warning threshold
     * @param   th: Die temperature warning threshold.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setTempWarning(TempWarning th);

    /**
     * @brief   Set top-off (termination) current threshold.
     *          When charge current tapers to this value during CV phase,
     *          the charger transitions to Top-Off mode.
     * @param   mA: Current threshold in mA. Range 1..63, step 1 mA.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setTopOffCurrent(uint8_t mA);

    /**
     * @brief   Set top-off timer duration.
     *          After entering Top-Off mode, charger remains charging for
     *          this duration before transitioning to DONE.
     * @param   timer: TopOffTimer enum value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setTopOffTimer(TopOffTimer timer);

    /**
     * @brief   Set fast charge safety timer duration.
     * @param   timer: FastChargeTimer enum value.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setFastChargeTimer(FastChargeTimer timer);

    /**
     * @brief   Set battery recharge voltage threshold.
     *          In DONE mode, if VBAT drops below (VBAT_REG - threshold)
     *          for 50 ms, charging restarts in CC mode.
     * @param   wide: false = 140 mV below VBAT_REG (default),
     *                 true  = 240 mV below VBAT_REG.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setRechargeThreshold(bool wide);

    /**
     * @brief   Enable or disable the battery charger.
     * @param   enable: true = enable charger, false = disable.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setChargeEnable(bool enable);

    /**
     * @brief   Read current charger status.
     * @retval  ChargeStatus enum value.
     */
    ChargeStatus getChargeStatus();

private:

    // Indicates that the chip is configured
    bool mIsConfigured { };

    // Hardware configuration
    uint8_t mAddr { };
    Interface::II2c *mpI2c { };

    // Number of a retries for I2C transactions
    static const uint32_t skI2cRetries = 4;

    // Voltage step to set output value
    static const uint32_t skVoltageStepMv = 25;

    // Voltage offset for SW2, LDO2
    static const uint32_t skVoltageOffsetMv = 1200;

    /**
     * @brief   Lock/Unlock charge register.
     * @param   lock: state. 'true' - lock, 'false' - unlock.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool chargeRegLock(bool lock);

    /**
     * @brief   Get bit code for target voltage for SW1.
     * @param   mV: target voltage.
     * @retval  Bit code.
     */
    uint8_t getOutBitsSw1(uint32_t mV);

    /**
     * @brief   Get bit code for target voltage for SW2, LDO2.
     * @param   mV: target voltage.
     * @param   offset: is ofset used to achieve target voltage.
     * @retval  Bit code.
     */
    uint8_t getOutBitsSw2Ldo2(uint32_t mV, bool &offset);

    /**
     * @brief   Get bit code for target voltage for LDO1.
     * @param   mV: target voltage.
     * @retval  Bit code.
     */
    uint8_t getOutBitsLdo1(uint32_t mV);

};

} /* namespace Hardware */

#endif /* __HARDWARE_PCA9420_HPP */
