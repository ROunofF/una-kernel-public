/**
 ******************************************************************************
 * @file    STUSB4500.cpp
 * @brief   USB Type-C / USB-PD sink controller.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "STUSB4500.hpp"

#define LOG_MODULE_PRX      "STUSB4500"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "OS.hpp"

namespace Hardware
{

// Expected value of DEVICE_ID register on a healthy STUSB4500.
static constexpr uint8_t skExpectedDeviceId = 0x25;

// TLOAD from datasheet: time for NVM defaults to load into registers after
// power-on or soft reset.
static constexpr uint32_t skTLoadMs = 35;

// Size of the ALERT_STATUS_1..PRT_STATUS contiguous region read as a single
// block to clear every alert latch in one transaction.
static constexpr uint8_t skAlertDrainLen = 11;

STUSB4500::STUSB4500(Interface::II2c& i2c, uint8_t devAddr)
    : mI2c(i2c)
    , mDevAddr(devAddr)
{
}

bool STUSB4500::isReady()
{
    return mI2c.isDeviceReady(mDevAddr);
}

bool STUSB4500::isPresent()
{
    // Fast ACK probe first so we silently skip out when the chip is
    // absent (legacy board) or unpowered, instead of triggering the
    // readReg retry/log storm that an immediate register access would
    // produce against a non-responding slave.
    if (!isReady()) {
        return false;
    }

    uint8_t deviceId = 0;
    if (!readReg(Regs::DEVICE_ID, deviceId)) {
        return false;
    }

    if (deviceId != skExpectedDeviceId) {
        LOG_DEBUG("unexpected DEVICE_ID 0x%02X\n", deviceId);
        return false;
    }

    return true;
}

bool STUSB4500::init(uint8_t mask)
{
    if (!writeReg(Regs::ALERT_STATUS_1_MASK, ~mask)) {
        return false;
    }

    // Drain ALERT_STATUS_1..PRT_STATUS so the first readAlerts() does not
    // return stale latches accumulated before the mask was applied.
    uint8_t scratch[skAlertDrainLen];
    return readReg(Regs::ALERT_STATUS_1, scratch, sizeof(scratch));
}

bool STUSB4500::readAlerts(Alerts& alerts)
{
    if (!readReg(Regs::ALERT_STATUS_1, alerts.status.d8)) {
        return false;
    }

    if (!readReg(Regs::ALERT_STATUS_1_MASK, alerts.mask.d8)) {
        return false;
    }

    alerts.status.d8 &= static_cast<uint8_t>(~alerts.mask.d8);

    return true;
}

bool STUSB4500::readStatus(Status& status)
{
    if (!readReg(Regs::PORT_STATUS_0,             status.portStatus0.d8)
     || !readReg(Regs::PORT_STATUS_1,             status.portStatus1.d8)
     || !readReg(Regs::TYPEC_MONITORING_STATUS_0, status.typecMonitoringStatus0.d8)
     || !readReg(Regs::TYPEC_MONITORING_STATUS_1, status.typecMonitoringStatus1.d8)
     || !readReg(Regs::CC_HW_FAULT_STATUS_0,      status.ccHWFaultStatus0.d8)
     || !readReg(Regs::CC_HW_FAULT_STATUS_1,      status.ccHWFaultStatus1.d8)
     || !readReg(Regs::PRT_STATUS,                status.prtStatus.d8)
     || !readReg(Regs::CC_STATUS,                 status.ccStatus.d8)) {
        return false;
    }

    return true;
}

bool STUSB4500::readStatus(Status& status, const Alerts& alerts)
{
    if (alerts.status.b.PORT_STATUS_AL) {
        if (!readReg(Regs::PORT_STATUS_0, status.portStatus0.d8)
         || !readReg(Regs::PORT_STATUS_1, status.portStatus1.d8)) {
            return false;
        }
    }

    if (alerts.status.b.TYPEC_MONITORING_STATUS_AL) {
        if (!readReg(Regs::TYPEC_MONITORING_STATUS_0, status.typecMonitoringStatus0.d8)
         || !readReg(Regs::TYPEC_MONITORING_STATUS_1, status.typecMonitoringStatus1.d8)) {
            return false;
        }
    }

    if (alerts.status.b.CC_HW_FAULT_STATUS_AL) {
        if (!readReg(Regs::CC_HW_FAULT_STATUS_0, status.ccHWFaultStatus0.d8)
         || !readReg(Regs::CC_HW_FAULT_STATUS_1, status.ccHWFaultStatus1.d8)) {
            return false;
        }
    }

    if (alerts.status.b.PRT_STATUS_AL) {
        if (!readReg(Regs::PRT_STATUS, status.prtStatus.d8)) {
            return false;
        }
    }

    if (!readReg(Regs::CC_STATUS, status.ccStatus.d8)) {
        return false;
    }

    return true;
}

bool STUSB4500::swResetByReg()
{
    if (!writeReg(Regs::RESET_CTRL, ResetCtrlBits::RESET_SW_EN)) {
        return false;
    }

    OS::Delay(skTLoadMs);

    if (!isReady()) {
        return false;
    }

    // Clear every clear-on-read alert/status latched during reset in one
    // transaction (ALERT_STATUS_1..PRT_STATUS is contiguous).
    uint8_t scratch[skAlertDrainLen];
    if (!readReg(Regs::ALERT_STATUS_1, scratch, sizeof(scratch))) {
        return false;
    }

    return writeReg(Regs::RESET_CTRL, ResetCtrlBits::RESET_SW_DIS);
}

bool STUSB4500::readReg(STUSB4500::Regs reg, uint8_t* buff, uint8_t len)
{
    uint32_t attemptsLeft = skI2cMaxAttempts;

    while (true) {
        if (mI2c.readMem(mDevAddr, static_cast<uint8_t>(reg), 1, buff, len)) {
            return true;
        }

        if (--attemptsLeft == 0) {
            LOG_ERROR("Failed to read reg 0x%02X\n", static_cast<uint8_t>(reg));
            return false;
        }

        LOG_WARNING("Retry\n");
    }
}

bool STUSB4500::readReg(STUSB4500::Regs reg, uint8_t& value)
{
    return readReg(reg, &value, 1);
}

bool STUSB4500::writeReg(STUSB4500::Regs reg, uint8_t* buff, uint8_t len)
{
    uint32_t attemptsLeft = skI2cMaxAttempts;

    while (true) {
        if (mI2c.writeMem(mDevAddr, static_cast<uint8_t>(reg), 1, buff, len)) {
            return true;
        }

        if (--attemptsLeft == 0) {
            LOG_ERROR("Failed to write reg 0x%02X\n", static_cast<uint8_t>(reg));
            return false;
        }

        LOG_WARNING("Retry\n");
    }
}

bool STUSB4500::writeReg(STUSB4500::Regs reg, uint8_t value)
{
    return writeReg(reg, &value, 1);
}

} /* namespace Hardware */
