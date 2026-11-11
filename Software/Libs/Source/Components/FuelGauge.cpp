/**
 ******************************************************************************
 * @file    FuelGauge.hpp
 * @brief   Fuel Gauge.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "FuelGauge.hpp"

#define LOG_MODULE_PRX      "FuelGauge"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "main.h"
#include "Gpio.hpp"
#include "I2c.hpp"

// Configuration defined in main.h
//
// The battery-hardware fallbacks below match the MAX17262 power-on defaults
// (datasheet), so a build with no main.h override encodes those registers
// back to the chip's reset values. The algorithm-tuning registers
// (FullSOCThr, SOCHold) are set to non-default values unconditionally, so
// checkProfile() on a fresh gauge reports a mismatch and the model is
// reloaded once to apply them.

#if !defined (BATTERY_CAPACITY)
#define BATTERY_CAPACITY            (1500)   // DesignCap default 0x0BB8 (3000 * 0.5 mAh)
#endif

#if !defined (BATTERY_TERM_CURRENT)
#define BATTERY_TERM_CURRENT        (250)    // IChgTerm default 0x0640 (1600 * 0.15625 mA)
#endif

#if !defined (BATTERY_EMPTY_VOLTAGE)
#define BATTERY_EMPTY_VOLTAGE       (3300)   // VEmpty.VE default from 0xA561
#endif

#if !defined (BATTERY_RECOVERY_VOLTAGE)
#define BATTERY_RECOVERY_VOLTAGE    (3880)   // VEmpty.VR default from 0xA561
#endif

#if !defined (POWER_BAT_VOLTAGE_MAX)
#define POWER_BAT_VOLTAGE_MAX       (4400)   // ModelCFG.VChg default 1 -> >4.25 V cell
#endif

// Design battery capacity, mAh.
static const uint16_t skBatteryCapacity = BATTERY_CAPACITY;

// EZ-algorithm tuning.
static const uint16_t skTermCurrent      = BATTERY_TERM_CURRENT;       // mA
static const uint16_t skEmptyVoltage     = BATTERY_EMPTY_VOLTAGE;      // mV
static const uint16_t skRecoveryVoltage  = BATTERY_RECOVERY_VOLTAGE;   // mV

// Algorithm-tuning registers (not battery-hardware specific).
// FullSOCThr (0x13), %: 80 = EZ Performance recommendation (UG6597 p.29) vs
// the 95% power-on default for custom-characterized models.
static const uint16_t skFullSocThr       = 80;
// SOCHold (0xD3): 99%HoldEn on, Empty Hold off (0x1000). Empty Hold (default
// 0x1002) would pin RepSOC at 1% down to VEmpty, removing the power-off margin.
static const uint16_t skSocHold          = 0x1000;

// ModelCFG.VChg: true for >4.25 V cells (high-voltage chemistry).
static constexpr bool  skHighVoltageBattery = (POWER_BAT_VOLTAGE_MAX > 4250);

bool FuelGauge::init()
{
    if (mInited) {
        return true;
    }

    mIsPresent = mMAX17262.isReady();
    if (mIsPresent == false) {
        LOG_ERROR("MAX17262 not found\n");
        return false;
    }

    mWasPowerOnReset = mMAX17262.isPowerOnReset();
    if (mWasPowerOnReset) {
        LOG_WARNING("Battery removal detected\n");
    }

    const Hardware::MAX17262::Config cfg = {
        skBatteryCapacity,      // designCapMah
        skTermCurrent,          // termCurrentMa
        skEmptyVoltage,         // emptyVoltageMv
        skRecoveryVoltage,      // recoveryVoltageMv
        skFullSocThr,           // fullSocThrPct
        skSocHold,              // socHold
        skHighVoltageBattery,   // highVoltageBattery
    };

    const Hardware::MAX17262::ProfileStatus status = mMAX17262.checkProfile(cfg);
    if (status == Hardware::MAX17262::ProfileStatus::UNKNOWN) {
        LOG_ERROR("failed to read the profile\n");
        return false;
    }

    if (status != Hardware::MAX17262::ProfileStatus::MATCHES) {
        LOG_WARNING("Profile mismatch - loading model\n");
        if (!mMAX17262.loadModel(cfg)) {
            LOG_ERROR("Failed to load MAX17262 model\n");
            return false;
        }
    }

    // Clear POR unconditionally on a successful init - whether we just
    // loaded the model or the profile already matched, the gauge is in
    // the right state and the power-cycle flag has been handled.
    mMAX17262.clearPowerOnFlag();

    mInited = true;
    LOG_INFO("Initialized\n");
    return true;
}

float FuelGauge::getBatteryLevel()
{
    if (mIsPresent == false) {
        return 0;
    }

    float batteryLevel = mMAX17262.getSoc();
    if (batteryLevel > 100) {
        batteryLevel = 100;
    }

    return batteryLevel;
}

float FuelGauge::getRawBatteryLevel()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getSoc();
}

float FuelGauge::getBatteryVoltage()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getVoltage();
}

float FuelGauge::getAvgBatteryVoltage()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getAvgVoltage();
}

float FuelGauge::getCapacity()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getCapacity();
}

float FuelGauge::getFullCapacity()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getFullCapacity();
}

float FuelGauge::getCurrent()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getCurrent();
}

float FuelGauge::getAvgCurrent()
{
    if (mIsPresent == false) {
        return 0;
    }

    return mMAX17262.getAvgCurrent();
}

uint16_t FuelGauge::readReg(uint8_t addr)
{
    uint16_t value;
    mMAX17262.readReg(addr, value);

    return value;
}


bool FuelGauge::getTelemetry(Hardware::MAX17262::Telemetry& t)
{
    if (mIsPresent == false) {
        return false;
    }

    return mMAX17262.readTelemetry(t);
}
