/**
 ******************************************************************************
 * @file    PCA9420.hpp
 * @brief   Power Management IC (PMIC).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "PCA9420.hpp"

#define LOG_MODULE_PRX      "PCA9420"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>

namespace Hardware
{

PCA9420::PCA9420(Interface::II2c *pI2c, uint8_t devAddr)
{
    config(pI2c, devAddr);
}

bool PCA9420::config(Interface::II2c *pI2c, uint8_t devAddr)
{
    assert(pI2c != nullptr);

    mpI2c = pI2c;
    mAddr = devAddr;

    mIsConfigured = true;

    return true;
}

bool PCA9420::isReady()
{
    assert(mIsConfigured == true);

    return mpI2c->isDeviceReady(mAddr);
}

bool PCA9420::readReg(uint8_t reg, uint8_t& v)
{
    assert(mIsConfigured == true);

    uint32_t retries = skI2cRetries;

    while (true) {
        if (mpI2c->readMem(mAddr, (uint8_t)reg, sizeof(reg), (uint8_t*)&v, sizeof(v))) {
            return true;
        }

        if (--retries == 0) {
            LOG_ERROR("Failed to read reg 0x%02X\n", reg);
            return false;
        }

        LOG_WARNING("Retry\n");
    }
}

bool PCA9420::writeReg(uint8_t reg, uint8_t v)
{
    assert(mIsConfigured == true);

    uint32_t retries = skI2cRetries;

    while (true) {
        if (mpI2c->writeMem(mAddr, (uint8_t)reg, sizeof(reg), (uint8_t*)&v, sizeof(v))) {
            return true;
        }

        if (--retries == 0) {
            LOG_ERROR("Failed to write reg(s) 0x%02X\n", reg);
            return false;
        }

        LOG_WARNING("Retry\n");
    }
}

bool PCA9420::reset()
{
    Reg::TopCntl3 reg { };
    uint8_t regNum = Reg::TOP_CNTL3;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.swReset = 1;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::enterShipMode()
{
    // The device runs in Mode Setting 0 after power-up; setting SHIP_EN in
    // its mode register triggers ship mode immediately, regardless of the
    // current mode.
    Reg::Modecfgx0 reg { };
    uint8_t regNum = Reg::MODECFG_0_0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.shipModeEn = 1;

    return writeReg(regNum, reg.reg);
}

bool PCA9420::setMode(PCA9420::Mode mode)
{
    Reg::TopCntl3 reg { };
    uint8_t regNum = Reg::TOP_CNTL3;

    if (!readReg(regNum, reg.reg)) {
        LOG_ERROR("failed to read TOP_CNTL3\n");
        return false;
    }

    switch (mode) {
        case Mode::MODE_0:
            reg.mode1I2c = 0;
            reg.mode0I2c = 0;
            break;
        case Mode::MODE_1:
            reg.mode1I2c = 0;
            reg.mode0I2c = 1;
            break;
        case Mode::MODE_2:
            reg.mode1I2c = 1;
            reg.mode0I2c = 0;
            break;
        case Mode::MODE_3:
            reg.mode1I2c = 1;
            reg.mode0I2c = 1;
            break;
        default:
            break;
    }

    if (!writeReg(regNum, reg.reg)) {
        LOG_ERROR("failed to read TOP_CNTL3\n");
        return false;
    }

    return true;
}

bool PCA9420::setModeSelectionByPin(PCA9420::Mode mode, bool enable)
{
    Reg::Modecfgx0 reg { };
    uint8_t regNum = Reg::MODECFG_0_0 + (static_cast<uint8_t>(mode) * 4);

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.selModeByPinEn = enable;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


bool PCA9420::setSwitchBackToMode0(PCA9420::Mode mode, bool enable)
{
    Reg::Modecfgx1 reg { };
    uint8_t regNum = Reg::MODECFG_0_1 + (static_cast<uint8_t>(mode) * 4);

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.onCfg = enable;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


bool PCA9420::setVoltage(PCA9420::Out out, PCA9420::Mode mode, uint32_t mV)
{
    bool offset = false;

    uint8_t regNum = (Reg::MODECFG_0_0 + static_cast<uint8_t>(out))
            + (static_cast<uint8_t>(mode) * 4);

    uint8_t regRaw = 0;
    if (!readReg(regNum, regRaw)) {
        return false;
    }

    switch (out) {
        case Out::SW1: {
            Reg::Modecfgx0 reg { };
            reg.reg = regRaw;
            reg.sw1Out = getOutBitsSw1(mV);
            regRaw = reg.reg;
        }
            break;
        case Out::SW2: {
            Reg::Modecfgx1 reg { };
            reg.reg = regRaw;
            reg.sw2OutLsb = getOutBitsSw2Ldo2(mV, offset);
            reg.sw2OutOffset = offset;
            regRaw = reg.reg;
        }
            break;
        case Out::LDO1: {
            Reg::Modecfgx2 reg { };
            reg.reg = regRaw;
            reg.ldo1Out = getOutBitsLdo1(mV);
            regRaw = reg.reg;
        }
            break;
        case Out::LDO2: {
            Reg::Modecfgx3 reg { };
            reg.reg = regRaw;
            reg.ldo2OutLsb = getOutBitsSw2Ldo2(mV, offset);
            reg.ldo2OutOffset = offset;
            regRaw = reg.reg;
        }
            break;
        default:
            break;
    }

    if (!writeReg(regNum, regRaw)) {
        return false;
    }

    return true;
}

bool PCA9420::enableOut(PCA9420::Out out, PCA9420::Mode mode, bool isOn)
{
    Reg::Modecfgx2 reg { };
    uint8_t regNum = Reg::MODECFG_0_2 + (static_cast<uint8_t>(mode) * 4);

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    switch (out) {
        case Out::SW1:
            reg.sw1En = isOn ? 1 : 0;
            break;
        case Out::SW2:
            reg.sw2En = isOn ? 1 : 0;
            break;
        case Out::LDO1:
            reg.ldo1En = isOn ? 1 : 0;
            break;
        case Out::LDO2:
            reg.ldo2En = isOn ? 1 : 0;
            break;
        default:
            break;
    }

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


bool PCA9420::setChargeCurrentLim(uint16_t mA)
{
    Reg::ChgCntl1 reg { };
    uint8_t regNum = Reg::CHG_CNTL1;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    // Fast charge current is set by values
    // from 0x00 to 0x3F
    // 0x00 - 0 mA
    // 0x3F - 315 mA
    // Step is 5 mA
    // For 100 mA. 100 / 5 = 20 (0x14)

    uint8_t chgVal = mA / 5;
    if(chgVal > 0x3F){
        // Set to max
        chgVal = 0x3F;
    }

    reg.iFastChg = chgVal;

    chargeRegLock(false);
    bool result= writeReg(regNum, reg.reg);
    chargeRegLock(true);

    return result;
}


bool PCA9420::setNtc(bool enable)
{
    Reg::ChgCntl0 reg { };
    uint8_t regNum = Reg::CHG_CNTL0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.ntcEn = enable;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


bool PCA9420::setNtcResSel(bool sel)
{
    Reg::ChgCntl6 reg { };
    uint8_t regNum = Reg::CHG_CNTL6;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.ntcResSel = sel ? 1 : 0;

    chargeRegLock(false);
    bool result = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    return result;
}


bool PCA9420::setLongGlitchOnTime(PCA9420::LongGlitchON time)
{
    Reg::TopCntl3 reg { };
    uint8_t regNum = Reg::TOP_CNTL3;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.onGltLon = (uint8_t)time;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::setVBAT(uint16_t value)
{
    const uint16_t vMin = 3600;
    const uint16_t vMax = 4600;

    if (value < vMin) {
        LOG_ERROR("VBAT value less than %d\n", (int)vMin);
        return false;
    }

    if (value > vMax) {
        LOG_ERROR("VBAT value more than %d\n", (int)vMax);
        return false;
    }

    Reg::ChgCntl5 reg{};
    uint8_t       regNum = Reg::CHG_CNTL5;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    const uint8_t step    = 20;

    int code = (value - vMin + (step / 2)) / step;

    reg.vBatReg = (uint8_t) (code & 0x3F);

    chargeRegLock(false);
    bool status = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    LOG_DEBUG("value = %d, status = %s\n", value, status ? "OK" : "FAIL");

    return status;
}

bool PCA9420::getVBAT(uint16_t& value)
{
    Reg::ChgCntl5 reg{};
    uint8_t       regNum = Reg::CHG_CNTL5;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    value = reg.vBatReg * 20 + 3600;

    return true;
}

bool PCA9420::isVinDetected()
{
    Reg::ChgStatus0 reg { };
    uint8_t regNum = Reg::CHG_STATUS_0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    return reg.vinOk != 0;
}


bool PCA9420::isVbatDetected()
{
    Reg::ChgStatus0 reg { };
    uint8_t regNum = Reg::CHG_STATUS_0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }
    return reg.vbatDetOk != 0;
}


bool PCA9420::isCharging()
{
    Reg::ChgStatus2 reg { };
    uint8_t regNum = Reg::CHG_STATUS_2;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    bool isCharging = false;

    /*
     * BAT_DETAIL_STATUS Battery conditions in details
     * 000: Battery missing, not attached
     * 001: Battery detection in-progress
     * 010: VBAT < VBAT_DEAD
     * 011: VBAT_DEAD < VBAT < VBAT_LOW
     * 100: VBAT_LOW < VBAT < (VBAT_REG - VBAT_RESTART)
     * 101: VBAT > (VBAT_REG - VBAT_RESTART)
     * 110: reserved
     * 111: battery charger is in reset
     *
     * BAT_CHG_STATUS Charge conditions in details:
     * 000: Charger in Idle State
     * 001: Charger in Dead-Battery State
     * 010: Charger in Low-Battery State
     * 011-100: Charger in Fast Charging state in either CC or CV
     * 101: Charger in Top-off State
     * 110: Charger in Done State
     * 111: Reserved
     */

    if ((reg.chgStatus > 0 && reg.chgStatus < 0x05) || (reg.batDetailStatus == 1)) {
        isCharging = true;
    }

    return isCharging;
}


bool PCA9420::enableIrq(Irq_t::Mask mask)
{
    bool result = true;
    uint8_t regMask = 0;

    // Unmask corresponding bits

    if (mask.int0.reg != 0) {
        result &= readReg(Reg::SUB_INT0_MASK, regMask);
        regMask &= (uint8_t)(~mask.int0.reg);
        result &= writeReg(Reg::SUB_INT0_MASK, regMask);
    }

    if (mask.int1.reg != 0) {
        result &= readReg(Reg::SUB_INT1_MASK, regMask);
        regMask &= (uint8_t)(~mask.int1.reg);
        result &= writeReg(Reg::SUB_INT1_MASK, regMask);
    }

    if (mask.int2.reg != 0) {
        result &= readReg(Reg::SUB_INT2_MASK, regMask);
        regMask &= (uint8_t)(~mask.int2.reg);
        result &= writeReg(Reg::SUB_INT2_MASK, regMask);
    }

    return result;
}


bool PCA9420::disableIrq(Irq_t::Mask mask)
{
    bool result = true;
    uint8_t regMask = 0;

    // Mask corresponding bits

    if (mask.int0.reg != 0) {
        result &= readReg(Reg::SUB_INT0_MASK, regMask);
        regMask |= mask.int0.reg;
        result &= writeReg(Reg::SUB_INT0_MASK, regMask);
    }

    if (mask.int1.reg != 0) {
        result &= readReg(Reg::SUB_INT1_MASK, regMask);
        regMask |= mask.int1.reg;
        result &= writeReg(Reg::SUB_INT1_MASK, regMask);
    }

    if (mask.int2.reg != 0) {
        result &= readReg(Reg::SUB_INT2_MASK, regMask);
        regMask |= mask.int2.reg;
        result &= writeReg(Reg::SUB_INT2_MASK, regMask);
    }

    return result;
}

bool PCA9420::setIrq(Irq_t::Mask mask)
{
    bool result = true;

    // Invert mask: '0' bit in mask register = interrupt enabled
    uint8_t regMask0 = (uint8_t)(~mask.int0.reg);
    uint8_t regMask1 = (uint8_t)(~mask.int1.reg);
    uint8_t regMask2 = (uint8_t)(~mask.int2.reg);

    result &= writeReg(Reg::SUB_INT0_MASK, regMask0);
    result &= writeReg(Reg::SUB_INT1_MASK, regMask1);
    result &= writeReg(Reg::SUB_INT2_MASK, regMask2);

    return result;
}

PCA9420::Irq_t PCA9420::getIrqFlags()
{
    Irq_t regs { };
    readReg(Reg::TOP_INT, regs.top.reg);
    readReg(Reg::SUB_INT0, regs.mask.int0.reg);
    readReg(Reg::SUB_INT1, regs.mask.int1.reg);
    readReg(Reg::SUB_INT2, regs.mask.int2.reg);
    return regs;
}


bool PCA9420::clearIrqFlags(Irq_t::Mask mask)
{
    bool result = writeReg(Reg::SUB_INT0, mask.int0.reg);
    result &= writeReg(Reg::SUB_INT1, mask.int1.reg);
    result &= writeReg(Reg::SUB_INT2, mask.int2.reg);
    return result;
}

bool PCA9420::setVinUnderVoltageLockout(PCA9420::VinUVLO threshold)
{
    Reg::TopCntl1 reg{};
    uint8_t       regNum = Reg::TOP_CNTL1;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.vinUvLockoutSel = (uint8_t)threshold;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::setAsysInputSource(PCA9420::AsysInput source)
{
    Reg::TopCntl1 reg{};
    uint8_t       regNum = Reg::TOP_CNTL1;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.asysInSel = (uint8_t)source;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::setVinILim(PCA9420::VinILim lim)
{
    Reg::TopCntl0 reg{};
    uint8_t       regNum = Reg::TOP_CNTL0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.vinIlimSel = (uint8_t)lim;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::setTempWarning(TempWarning th)
{
    Reg::TopCntl2 reg{};
    uint8_t       regNum = Reg::TOP_CNTL2;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.dieTempWarning = (uint8_t)th;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}

bool PCA9420::chargeRegLock(bool isLock)
{
    Reg::ChgCntl0 reg { };
    uint8_t regNum = Reg::CHG_CNTL0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.chgLock = isLock ? 0x0 : 0x15;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


uint8_t PCA9420::getOutBitsSw1(uint32_t mV)
{
    const uint32_t min = 500;
    const uint32_t max = 1500;
    const uint32_t max2 = 1800;

    if (mV >= max2) {
        return 0x3F;    // 1.8V
    }

    if (mV >= max && mV < max2) {
        return 0x28;    // 1.5V
    }

    if (mV <= min) {
        return 0;       // 0.5V
    }

    return (mV - min) / skVoltageStepMv;
}


uint8_t PCA9420::getOutBitsSw2Ldo2(uint32_t mV, bool &offset)
{
    const uint32_t min = 1500;
    const uint32_t max = 2100;

    if (mV <= min) {
        return 0;       // 1.5V
    }

    if (mV >= (max + skVoltageOffsetMv)) {
        offset = true;
        return 0x3F;    // 2.1 + 1.2 = 3.3V
    }

    if (mV >= max && mV < (min + skVoltageOffsetMv)) { // 2.1 - 2.7
        return 0x3F;    // 2.1
    }

    if (mV >= (min + skVoltageOffsetMv)) {
        mV -= skVoltageOffsetMv;
        offset = true;
    }

    return (mV - min) / skVoltageStepMv;
}


uint8_t PCA9420::getOutBitsLdo1(uint32_t mV)
{
    const int32_t min = 1700;
    const int32_t max = 1900;

    if (mV >= max) {
        return 0x0F;    // 1.9V
    }

    if (mV <= min) {
        return 0;       // 1.7V
    }

    return (mV - min) / skVoltageStepMv;
}


bool PCA9420::setTopOffCurrent(uint8_t mA)
{
    Reg::ChgCntl2 reg { };
    uint8_t regNum = Reg::CHG_CNTL2;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    // Range: 1..63 mA, step 1 mA
    uint8_t val = mA;
    if (val > 63) {
        val = 63;
    }
    if (val < 1) {
        val = 1;
    }

    reg.iTapOffChg = val;

    chargeRegLock(false);
    bool result = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    LOG_DEBUG("TopOff current = %d mA, status = %s\n",
              (int)val, result ? "OK" : "FAIL");

    return result;
}


bool PCA9420::setTopOffTimer(TopOffTimer timer)
{
    Reg::ChgCntl6 reg { };
    uint8_t regNum = Reg::CHG_CNTL6;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.timTopOff = static_cast<uint8_t>(timer);

    chargeRegLock(false);
    bool result = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    return result;
}


bool PCA9420::setFastChargeTimer(FastChargeTimer timer)
{
    Reg::ChgCntl6 reg { };
    uint8_t regNum = Reg::CHG_CNTL6;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.iChgFastTim = static_cast<uint8_t>(timer);

    chargeRegLock(false);
    bool result = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    return result;
}


bool PCA9420::setRechargeThreshold(bool wide)
{
    Reg::ChgCntl5 reg { };
    uint8_t regNum = Reg::CHG_CNTL5;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.vBatRestart = wide ? 1 : 0;

    chargeRegLock(false);
    bool result = writeReg(regNum, reg.reg);
    chargeRegLock(true);

    return result;
}


bool PCA9420::setChargeEnable(bool enable)
{
    Reg::ChgCntl0 reg { };
    uint8_t regNum = Reg::CHG_CNTL0;

    if (!readReg(regNum, reg.reg)) {
        return false;
    }

    reg.chgEn = enable ? 1 : 0;

    if (!writeReg(regNum, reg.reg)) {
        return false;
    }

    return true;
}


PCA9420::ChargeStatus PCA9420::getChargeStatus()
{
    Reg::ChgStatus2 reg { };

    if (!readReg(Reg::CHG_STATUS_2, reg.reg)) {
        return ChargeStatus::IDLE;
    }

    return static_cast<ChargeStatus>(reg.chgStatus);
}

} /* namespace Hardware */
