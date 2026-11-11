/**
 ******************************************************************************
 * @file    Pmic.hpp
 * @brief   Class for configuring PMIC to suit project needs.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Pmic.hpp"

#define LOG_MODULE_PRX      "Pmic"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>

#include "main.h"
// Configuration defined in main.h
#if !defined (BATTERY_CHARGE_CURRENT)
#define BATTERY_CHARGE_CURRENT  (100)
#endif

#if !defined (POWER_RAIL_MV_SW1)
#define POWER_RAIL_MV_SW1       (1000)
#endif

#if !defined (POWER_RAIL_MV_SW2)
#define POWER_RAIL_MV_SW2       (1800)
#endif

#if !defined (POWER_RAIL_MV_LDO1)
#define POWER_RAIL_MV_LDO1      (1800)
#endif

#if !defined (POWER_RAIL_MV_LDO2)
#define POWER_RAIL_MV_LDO2      (3300)
#endif

#if !defined (POWER_BAT_VOLTAGE_MAX)
#define POWER_BAT_VOLTAGE_MAX      (4200)
#endif

// Battery charge current, mA
static const uint32_t skChargeCurrent = BATTERY_CHARGE_CURRENT;

// Battery max charge voltage, mV
static const uint32_t skVBat        = POWER_BAT_VOLTAGE_MAX;
static const uint32_t skITermCharge = 10;

// Rail voltages
static const uint32_t skVoltageSw1  = POWER_RAIL_MV_SW1;
static const uint32_t skVoltageSw2  = POWER_RAIL_MV_SW2;
static const uint32_t skVoltageLdo1 = POWER_RAIL_MV_LDO1;
static const uint32_t skVoltageLdo2 = POWER_RAIL_MV_LDO2;


bool Pmic::init()
{
    assert(mpIrq != nullptr);

    if (mInited) {
        return true;
    }

    // Configure PMIC interrupt pin
    mpIrq->init(Interface::IGpi::Pull::UP, Interface::IExti::Edge::FALLING, 5);
    mpIrq->setExtiHandler(this);
    mpIrq->clearInt();
    mpIrq->enableInt(true);

    mIsPresent = mPCA9420.isReady();

    if (mIsPresent == false) {
        LOG_ERROR("PCA9420 not found\n");
        return false;
    }

    // Do not boot the watch on a bare battery insertion. If the PMIC
    // powered up only because a battery was attached, park it in ship
    // mode; it will power up later on USB or an ON-key press.
    if (shouldEnterShipMode()) {
        LOG_INFO("Battery-only power-up, entering ship mode\n");
        mPCA9420.enterShipMode();
        // The chip now runs its power-down sequence and the MCU loses
        // power shortly. A failed ship entry falls back to a normal boot.
        return false;
    }

    // Set charge current
    mPCA9420.setChargeCurrentLim(skChargeCurrent);

    // Disable NTC control to detect the battery
    mPCA9420.setNtc(false);

    // Configure long press timeout for ON button
    mPCA9420.setLongGlitchOnTime(Hardware::PCA9420::LongGlitchON::TIME_16S);

    // Max voltage on the battery (in mV)
    mPCA9420.setVBAT(skVBat);

    // Configure charger termination for proper MAX17262 end-of-charge detection.
    // ICHG_TOPOFF: current threshold to enter Top-Off mode.
    // Battery spec: CV to 0.01C cutoff = 2.8 mA (280 mAh * 0.01).
    // Set to 10 mA to match battery spec and ensure MAX17262 sees IChgTerm (14 mA)
    // transition before PMIC enters Top-Off.
    mPCA9420.setTopOffCurrent(skITermCharge);

    // T_TOPOFF: additional charging time after entering Top-Off.
    // Use maximum (19.2 min) to ensure battery is fully topped off.
    mPCA9420.setTopOffTimer(Hardware::PCA9420::TopOffTimer::TIME_19_2MIN);

    // Fast charge safety timer: set to maximum (9 hours) to avoid premature
    // charge termination for deeply discharged batteries.
    mPCA9420.setFastChargeTimer(Hardware::PCA9420::FastChargeTimer::TIME_9H);

    // Recharge threshold: when VBAT drops 140 mV below VBAT_REG in DONE mode,
    // restart charging. This prevents long idle periods on cable that cause
    // MAX17262 FullCapRep drift.
    mPCA9420.setRechargeThreshold(false);

    // Set ASYS powered from both VBAT and VIN.
    mPCA9420.setAsysInputSource(Hardware::PCA9420::AsysInput::ASYS_INPUT_VBAT_VIN);

    // Enable the battery charger.
    mPCA9420.setChargeEnable(true);

    // Set under-voltage lockout threshold (falling) on VIN to 3.3V
    // This threshold must be lower than the minimum operating voltage
    // set in the fuelgauge (3.4V)
    mPCA9420.setVinUnderVoltageLockout(Hardware::PCA9420::VinUVLO::VIN_UVLO_3300);

    // Set VIN_ILIM_SEL to 001 (222mA min)
    mPCA9420.setVinILim(Hardware::PCA9420::VinILim::VIN_LIM_255);

    // Set a Die temperature warning threshold to 75 °C
    mPCA9420.setTempWarning(Hardware::PCA9420::TempWarning::TEMP_WARN_75);

    // Configure modes
    // 1. Switch to mode 0 (ACTIVE) when button pressed from any mode
    // 2. Enable all rails in modes 0,1,3
    // 3. Enable only LDO1 (RTC) in mode 2 (POWER OFF)
    // 4. Enable selection mode in mode 2 by pins in order to run when
    //    USB is connected

    // Mode 0
    mPCA9420.setModeSelectionByPin(Hardware::PCA9420::Mode::MODE_0, false);
    mPCA9420.setSwitchBackToMode0(Hardware::PCA9420::Mode::MODE_0, true);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_0, skVoltageSw1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_0, skVoltageSw2);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_0, skVoltageLdo1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_0, skVoltageLdo2);
    // Enable in Mode_0 all voltages
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_0, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_0, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_0, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_0, true);

    // Mode 1
    mPCA9420.setModeSelectionByPin(Hardware::PCA9420::Mode::MODE_1, false);
    mPCA9420.setSwitchBackToMode0(Hardware::PCA9420::Mode::MODE_1, true);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_1, skVoltageSw1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_1, skVoltageSw2);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_1, skVoltageLdo1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_1, skVoltageLdo2);
    // Enable in Mode_1 all voltages
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_1, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_1, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_1, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_1, true);

    // Mode 2
    mPCA9420.setModeSelectionByPin(Hardware::PCA9420::Mode::MODE_2, true);
    mPCA9420.setSwitchBackToMode0(Hardware::PCA9420::Mode::MODE_2, true);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_2, skVoltageSw1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_2, skVoltageSw2);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_2, skVoltageLdo1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_2, skVoltageLdo2);
    // Enable in Mode_2 LDO1 (RTC) only
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_2, false);
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_2, false);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_2, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_2, false);

    // Mode 3
    mPCA9420.setModeSelectionByPin(Hardware::PCA9420::Mode::MODE_3, false);
    mPCA9420.setSwitchBackToMode0(Hardware::PCA9420::Mode::MODE_3, true);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_3, skVoltageSw1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_3, skVoltageSw2);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_3, skVoltageLdo1);
    mPCA9420.setVoltage(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_3, skVoltageLdo2);
    // Enable in Mode_3 all voltages
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW1, Hardware::PCA9420::Mode::MODE_3, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::SW2, Hardware::PCA9420::Mode::MODE_3, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO1, Hardware::PCA9420::Mode::MODE_3, true);
    mPCA9420.enableOut(Hardware::PCA9420::Out::LDO2, Hardware::PCA9420::Mode::MODE_3, true);

    // Used interrupts:
    // Vin changed
    Hardware::PCA9420::Irq_t::Mask irq { };
    irq.int0.vin = 1;
//    irq.int0.asysPrewarning = 1;  // TODO: Make a correct flag clearing before use
//    irq.int0.tempPrewarning = 1;
    irq.int1.chgOk = 1;
    mPCA9420.setIrq(irq);

    mInited = true;

    LOG_INFO("Initialized\n");

    return true;
}

bool Pmic::shouldEnterShipMode()
{
    // SUB_INT0_MASK value right after a PCA9420 cold power-up (defaults).
    // init() reprograms it, so any other value means the PMIC retained
    // its configuration across this start.
    static const uint8_t skSubInt0MaskReset = 0x3F;

    // USB attached is an intentional power source - keep the device on.
    if (mPCA9420.isVinDetected()) {
        return false;
    }

    // A SUB_INT0_MASK that differs from its power-on default means the
    // PMIC was already configured by a previous run and only the MCU
    // restarted (e.g. a software reset) - this was not a cold power-up.
    uint8_t intMask = 0;
    if (!mPCA9420.readReg(Hardware::PCA9420::Reg::SUB_INT0_MASK, intMask)) {
        return false;
    }
    if (intMask != skSubInt0MaskReset) {
        return false;
    }

    // Any latched reset/wake cause means a known event triggered this
    // start (long-press, watchdog, SW reset, ship exit, thermal), not a
    // bare battery insertion.
    Hardware::PCA9420::Reg::ResetMon mon { };
    if (!mPCA9420.readReg(Hardware::PCA9420::Reg::RESET_MON, mon.reg)) {
        return false;
    }
    if (mon.longOnDone || mon.wdRstDone || mon.swRstDone ||
        mon.shipExitDone || mon.thmStdnDone) {
        return false;
    }

    return true;
}

bool Pmic::isUsbDetected()
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.isVinDetected();
}

bool Pmic::isBatteryDetected()
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.isVbatDetected();
}

bool Pmic::isCharging()
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.isCharging();
}

bool Pmic::setChargeCurrent(uint16_t mA)
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.setChargeCurrentLim(mA);
}

bool Pmic::powerOff()
{
    return mPCA9420.setMode(Hardware::PCA9420::Mode::MODE_2);
}

bool Pmic::isChargeDone()
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.getChargeStatus() == Hardware::PCA9420::ChargeStatus::DONE;
}

bool Pmic::setChargeEnable(bool enable)
{
    if (mIsPresent == false) {
        return false;
    }

    return mPCA9420.setChargeEnable(enable);
}

bool Pmic::setAsysInputSource(AsysInput source)
{
    if (mIsPresent == false) {
        return false;
    }

    Hardware::PCA9420::AsysInput hwSource;

    switch (source) {
        case AsysInput::VBAT_VIN:
            hwSource = Hardware::PCA9420::AsysInput::ASYS_INPUT_VBAT_VIN;
            break;
        case AsysInput::VBAT:
            hwSource = Hardware::PCA9420::AsysInput::ASYS_INPUT_VBAT;
            break;
        default:
            return false;
    }

    return mPCA9420.setAsysInputSource(hwSource);
}

Pmic::IrqFlags Pmic::clearIrq()
{
    IrqFlags flags;
    Hardware::PCA9420::Irq_t irq = mPCA9420.getIrqFlags();

    // Map hardware flags to abstract structure
    flags.vinChanged          = irq.mask.int0.vin;
    flags.chargeStatusChanged = irq.mask.int1.chgOk;

    LOG_DEBUG("top  = 0x%02x\n", irq.top.reg);
    LOG_DEBUG("int0 = 0x%02x\n", irq.mask.int0.reg);
    LOG_DEBUG("int1 = 0x%02x\n", irq.mask.int1.reg);
    LOG_DEBUG("int2 = 0x%02x\n", irq.mask.int2.reg);

    mPCA9420.clearIrqFlags(irq.mask);

    return flags;
}

bool Pmic::isIrqAsserted()
{
    assert(mpIrq != nullptr);

    return (mpIrq->get() == false);
}

void Pmic::extiHandler(Interface::IExti *pExti,
            Interface::IExti::Edge edge)
{
    if (mpIrqHandler != nullptr) {
        mpIrqHandler->onIrq();
    }
}
