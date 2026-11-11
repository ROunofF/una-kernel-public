/**
 ******************************************************************************
 * @file    MAX17262.cpp
 * @brief   1-Cell Fuel Gauge.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MAX17262.hpp"

#define LOG_MODULE_PRX      "MAX17262"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include "System.hpp"

namespace Hardware
{

MAX17262::MAX17262(Interface::II2c *pI2c, uint8_t devAddr)
{
    config(pI2c, devAddr);
}

bool MAX17262::config(Interface::II2c *pI2c, uint8_t devAddr)
{
    assert(pI2c != nullptr);

    mpI2c = pI2c;
    mAddr = devAddr;

    mIsConfigured = true;

    return true;
}

bool MAX17262::isReady()
{
    if (!mIsConfigured) {
        LOG_ERROR("not configured\n");
        assert(false);
    }

    return mpI2c->isDeviceReady(mAddr);
}

bool MAX17262::readReg(uint8_t reg, uint16_t &v)
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

bool MAX17262::writeReg(uint8_t reg, uint16_t v)
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

bool MAX17262::readBlock(uint8_t startReg, uint8_t* buf, uint8_t regCount)
{
    assert(mIsConfigured == true);

    uint32_t       retries = skI2cRetries;
    const uint16_t size    = static_cast<uint16_t>(regCount) * 2;

    while (true) {
        if (mpI2c->readMem(mAddr, startReg, sizeof(startReg), buf, size)) {
            return true;
        }

        if (--retries == 0) {
            LOG_ERROR("Failed to read block at 0x%02X\n", startReg);
            return false;
        }

        LOG_WARNING("Retry\n");
    }
}

bool MAX17262::readTelemetry(Telemetry& t)
{
    uint8_t buf[skTelemetryBlockBytes];
    if (!readBlock(0x00, buf, skTelemetryBlockRegs)) {
        return false;
    }

    auto raw = [&buf](uint8_t reg) -> uint16_t {
        return static_cast<uint16_t>(buf[reg * 2] | (buf[reg * 2 + 1] << 8));
    };
    auto sraw = [&raw](uint8_t reg) -> int16_t {
        return static_cast<int16_t>(raw(reg));
    };

    t.status     = raw(0x00);
    t.repSoc     = raw(0x06)  * kSocScaler;
    t.temp       = sraw(0x08) * kTempScaler;
    t.vCell      = raw(0x09)  * kVoltageScaler;
    t.current    = sraw(0x0A) * kCurrentScaler;
    t.avgCurrent = sraw(0x0B) * kCurrentScaler;
    t.qResidual  = raw(0x0C)  * kCapScaler;
    t.fullCapRep = raw(0x10)  * kCapScaler;
    t.cycles     = raw(0x17);
    t.avgVCell   = raw(0x19)  * kVoltageScaler;
    t.fullCapNom = raw(0x23)  * kCapScaler;
    t.rComp0     = raw(0x38);
    t.tempCo     = raw(0x39);
    t.fStat      = raw(0x3D);
    t.dQAcc      = raw(0x45);
    t.dPAcc      = raw(0x46);
    t.qh         = sraw(0x4D) * kCapScaler;

    return true;
}

bool MAX17262::isPowerOnReset()
{
    uint16_t reg = 0;
    if (!readReg(0x00, reg)) {
        return false;
    }

    return (reg & 0x02) != 0;
}

bool MAX17262::clearPowerOnFlag()
{
    // Read Status, clear only the POR bit (D1) and write it back,
    // preserving the other flags, then verify POR is cleared.
    uint16_t reg = 0;
    if (!readReg(0x00, reg)) {
        return false;
    }

    reg &= 0xFFFD;

    if (!writeReg(0x00, reg)) {
        return false;
    }

    if (!readReg(0x00, reg)) {
        return false;
    }

    return (reg & 0x0002) == 0;
}

bool MAX17262::loadModel(const Config& cfg)
{
    // Wait for FStat.DNR (Data Not Ready) to clear. This bit is set at
    // cell insertion and clears once the gauge has updated its output
    // registers (~710 ms from power-up).
    uint32_t startTick = getTick();
    while (true) {
        if ((getTick() - startTick) >= 750) {
            return false;
        }

        uint16_t reg;
        if (!readReg(0x3D, reg)) {
            return false;
        }

        if ((reg & 0x01) == 0) {
            break;
        }

        delay(10);
    }

    // Store original HibCFG so we can restore it after the model load.
    uint16_t hibCFG = 0;
    if (!readReg(0xBA, hibCFG)) {
        return false;
    }

    bool status = true;
    status &= writeReg(0x60, 0x90); // Exit Hibernate Mode step 1
    status &= writeReg(0xBA, 0x00); // Exit Hibernate Mode step 2
    status &= writeReg(0x60, 0x00); // Exit Hibernate Mode step 3

    const MAX17262::ProfileItem profile[] = {
            {0x18, encodeDesignCap(cfg.designCapMah)},
            {0x1E, encodeTermCurrent(cfg.termCurrentMa)},
            {0x3A, encodeVEmpty(cfg.emptyVoltageMv, cfg.recoveryVoltageMv)},
            {0x13, encodeFullSocThr(cfg.fullSocThrPct)},
            {0xD3, cfg.socHold},
            {0xDB, static_cast<uint16_t>(encodeModelCfg(cfg.highVoltageBattery) | kModelCFG_RefreshRequest)}
    };

    if (!writeProfile(profile, sizeof(profile) / sizeof(profile[0]))) {
        return false;
    }

    // Poll ModelCFG.Refresh - the gauge self-clears it once the model
    // is loaded.
    startTick = getTick();
    while (true) {
        if ((getTick() - startTick) >= 500) {
            return false;
        }

        uint16_t reg;
        if (!readReg(0xDB, reg)) {
            return false;
        }

        if ((reg & 0x8000) == 0) {
            break;
        }

        delay(10);
    }

    // Wipe stale learned state. loadModel runs only on a profile mismatch, so
    // the old learning belongs to the previous config; relearn from scratch.
    resetLearnedState(encodeDesignCap(cfg.designCapMah));

    // Restore the original HibCFG value.
    status &= writeReg(0xBA, hibCFG);

    return status && isBattDetected();
}

void MAX17262::resetLearnedState(uint16_t designCapRaw)
{
    // Reset the capacity-learning accumulators to design defaults. The model
    // Refresh reloads the OCV curve but keeps FullCap/dQAcc/dPAcc/Cycles, so
    // they must be cleared explicitly. dPAcc is the fixed value Maxim pairs
    // with dQAcc = DesignCap/2; FullCapNom is derived from these, so retry
    // until it reads back as DesignCap. (UG6595 section 2.3.5.)
    writeReg(0x10, designCapRaw);   // FullCapRep

    uint32_t retries = skI2cRetries;
    while (true) {
        writeReg(0x45, static_cast<uint16_t>(designCapRaw / 2)); // dQAcc
        writeReg(0x46, kDPAccReset);                             // dPAcc
        delay(10);
        writeReg(0x23, designCapRaw);                            // FullCapNom

        uint16_t fcn = 0;
        if (readReg(0x23, fcn) && fcn == designCapRaw) {
            break;
        }
        if (--retries == 0) {
            LOG_WARNING("FullCapNom reset not verified\n");
            break;
        }
    }

    writeReg(0x17, 0x0000);          // Cycles
    writeReg(0x38, kRComp0Default);  // RComp0  (learned resistance)
    writeReg(0x39, kTempCoDefault);  // TempCo  (its temperature coefficient)

    LOG_INFO("Learned state reset to design defaults\n");
}

bool MAX17262::setDesignCap(uint16_t mah)
{
    return writeReg(0x18, encodeDesignCap(mah));
}

bool MAX17262::isBattDetected()
{
    uint16_t reg = 0;
    if (!readReg(0x00, reg)) {
        return false;
    }

    return (reg & 0x0008) == 0;
}

float MAX17262::getSoc()
{
    uint16_t reg = 0;
    if (!readReg(0x06, reg)) {
        return 0;
    }
    return static_cast<float>(reg) * kSocScaler;
}

float MAX17262::getCapacity()
{
    uint16_t reg = 0;
    if (!readReg(0x05, reg)) {
        return 0;
    }
    return static_cast<float>(reg) * kCapScaler;
}

float MAX17262::getFullCapacity()
{
    uint16_t reg = 0;
    if (!readReg(0x10, reg)) {
        return 0;
    }
    return static_cast<float>(reg) * kCapScaler;
}

float MAX17262::getVoltage()
{
    uint16_t reg = 0;
    if (!readReg(0x09, reg)) {
        return 0;
    }
    return static_cast<float>(reg) * kVoltageScaler;
}

float MAX17262::getAvgVoltage()
{
    uint16_t reg = 0;
    if (!readReg(0x19, reg)) {
        return 0;
    }
    return static_cast<float>(reg) * kVoltageScaler;
}

float MAX17262::getCurrent()
{
    uint16_t reg = 0;
    if (!readReg(0x0A, reg)) {
        return 0;
    }
    return static_cast<float>(static_cast<int16_t>(reg)) * kCurrentScaler;
}

float MAX17262::getAvgCurrent()
{
    uint16_t reg = 0;
    if (!readReg(0x0B, reg)) {
        return 0;
    }
    return static_cast<float>(static_cast<int16_t>(reg)) * kCurrentScaler;
}

MAX17262::ProfileStatus MAX17262::checkProfile(const Config& cfg)
{
    const MAX17262::ProfileItem profile[] = {
            {0x18, encodeDesignCap(cfg.designCapMah)},
            {0x1E, encodeTermCurrent(cfg.termCurrentMa)},
            {0x3A, encodeVEmpty(cfg.emptyVoltageMv, cfg.recoveryVoltageMv)},
            {0x13, encodeFullSocThr(cfg.fullSocThrPct)},
            {0xD3, cfg.socHold},
            {0xDB, encodeModelCfg(cfg.highVoltageBattery)}
    };

    return checkProfile(profile, sizeof(profile) / sizeof(profile[0]));
}

uint32_t MAX17262::getTick()
{
    return Driver::System::GetTimeMs();
}

void MAX17262::delay(uint32_t ms)
{
    Driver::System::Delay(ms);
}

MAX17262::ProfileStatus MAX17262::checkProfile(const ProfileItem* profile, uint8_t size)
{
    for (uint8_t idx = 0; idx < size; ++idx) {
        uint16_t value;
        if (!readReg(profile[idx].reg, value)) {
            return ProfileStatus::UNKNOWN;
        }

        if (profile[idx].value != value) {
            LOG_DEBUG("reg 0x%02X should have value 0x%04X but it has 0x%04X\n", profile[idx].reg,
                                                                                 profile[idx].value,
                                                                                 value);
            return ProfileStatus::NO_MATCHES;
        }
    }

    return ProfileStatus::MATCHES;
}

bool MAX17262::writeProfile(const ProfileItem* profile, uint8_t size)
{
    for (uint8_t idx = 0; idx < size; ++idx) {
        if (!writeReg(profile[idx].reg, profile[idx].value)) {
            return false;
        }
    }

    return true;
}

} /* namespace Hardware */
