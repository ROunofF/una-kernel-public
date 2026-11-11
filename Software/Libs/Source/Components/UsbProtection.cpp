/**
 ******************************************************************************
 * @file    UsbProtection.cpp
 * @brief   USB Type-C protection and fault monitoring interface.
 ******************************************************************************
 *
 * Provides control and status monitoring for the USB protection path,
 * including STUSB4500 device, VBUS control, DAM line switching,
 * and USB fault interrupt handling.
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "UsbProtection"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "UsbProtection.hpp"

UsbProtection::UsbProtection()
    : mIsConfigured(false)
    , mIsArmed(false)
    , mDamEnablePin(nullptr)
    , mSTUSB4500(nullptr)
    , mSTUSB4500Status{}
    , mUsbFaultExti(nullptr)
    , mMode(Mode::Unknown)
    , mI2CErrorsCount(0)
    , mFailedDetectCycles(0)
    , mDataMutex()
{
    mDataMutex.init(LOG_MODULE_PRX);
}

void UsbProtection::config(Hardware::STUSB4500& STUSB4500,
                           Interface::IGpo&     damEnablePin,
                           Interface::IExti&    usbFaultExti)
{
    mSTUSB4500    = &STUSB4500;
    mDamEnablePin = &damEnablePin;
    mUsbFaultExti = &usbFaultExti;

    LOG_DEBUG("configured\n");

    mIsConfigured = true;
}

UsbProtection::Mode UsbProtection::getMode()
{
    OS::MutexCS cs(mDataMutex);

    return mMode;
}

void UsbProtection::onUsbConnected()
{
    if (!mIsConfigured) {
        LOG_WARNING("component not configured\n");
        return;
    }

    Mode mode;
    {
        OS::MutexCS cs(mDataMutex);
        mode = mMode;
    }

    if (mode == Mode::Bypassed) {
        return;     // legacy board, no chip to talk to
    }

    // STUSB4500 is powered from VBUS and loses its register context on
    // every unplug, so on each fresh attach we must wait for it to come
    // back online before talking to it:
    //   - TLOAD ~30 ms for NVM defaults to load into registers,
    //   - a few more ms for the Type-C attach state machine to classify
    //     the connected accessory (SNK_ATT vs DBG_ATT). Without this,
    //     an immediate readStatus() on hot plug catches NONE_ATT and
    //     DAM stays invisible until the next chip-internal event.
    OS::Delay(35);

    if (mode == Mode::Unknown) {
        // First few connects after boot: classify the board variant.
        // isPresent() already does up to skI2cMaxAttempts I2C tries
        // inside its readReg, so a single failed call counts as "one
        // detect cycle gave up". We require kMaxFailedDetectCycles such
        // cycles before locking the board as legacy, so transient I2C
        // glitches at first plug can recover on the next plug instead
        // of disabling USB-protection for the rest of the boot.
        if (!mSTUSB4500->isPresent()) {
            uint8_t cycles;
            {
                OS::MutexCS cs(mDataMutex);
                cycles = ++mFailedDetectCycles;
            }

            if (cycles >= kMaxFailedDetectCycles) {
                OS::MutexCS cs(mDataMutex);
                mMode = Mode::Bypassed;
                LOG_INFO("STUSB4500 absent after %u attempts; assuming legacy Rev 2.1 board\n",
                         cycles);
            } else {
                LOG_WARNING("STUSB4500 probe failed (cycle %u/%u); will retry on next plug\n",
                            cycles, kMaxFailedDetectCycles);
            }
            return;
        }

        {
            OS::MutexCS cs(mDataMutex);
            mMode = Mode::Active;
            mI2CErrorsCount     = 0;
            mFailedDetectCycles = 0;
        }
        LOG_INFO("STUSB4500 detected; Mode=Active\n");
    }

    // Mode::Active path runs on every plug. STUSB4500's ALERT mask must
    // be re-written and the EXTI re-armed because the chip lost both on
    // the previous unplug.
    if (!probeAndConfigure()) {
        LOG_ERROR("failed to (re-)configure STUSB4500\n");
    }
}

void UsbProtection::onUsbDisconnected()
{
    if (!mIsConfigured) {
        return;
    }

    // Note: we intentionally do NOT mask STUSB alerts here. If STUSB is
    // still alive and holding ALERT (standing CC fault while the cable is
    // powered), Power::hIrqPwrLine retries quenchStusbAlerts() while the
    // line stays asserted. On a real unplug STUSB is dead and there is
    // nothing to mask.

    disarmExti();

    // Drop any pending fault latch from this session; the next plug
    // starts from a clean state.
    mFaultLatched.store(false, std::memory_order_release);

    OS::MutexCS cs(mDataMutex);
    mSTUSB4500Status = {};
    // Don't let transient I2C errors during the disconnect transition
    // accumulate into Mode::Bypassed across plug/unplug cycles.
    mI2CErrorsCount = 0;
}

bool UsbProtection::probeAndConfigure()
{
    // Mark the interrupts that should be handled by STUSB4500
    Hardware::STUSB4500::ALERT_STATUS_1_MASK maskSTUSB4500{};
    maskSTUSB4500.b.PRT_STATUS_AL              = 1;
    maskSTUSB4500.b.CC_HW_FAULT_STATUS_AL      = 1;
    maskSTUSB4500.b.TYPEC_MONITORING_STATUS_AL = 1;
    maskSTUSB4500.b.PORT_STATUS_AL             = 1;

    if (!mSTUSB4500->init(maskSTUSB4500.d8)) {
        LOG_ERROR("failed to init the STUSB4500\n");
        return false;
    }

    // Seed the cached status with the chip's current state. Necessary
    // on warm MCU reset where STUSB stays powered and its alert latch
    // is already empty: without this, alert-driven readStatus() would
    // skip unrelated registers (PORT_STATUS_1 etc.) and DAM/attach
    // state would remain invisible until the next attach event.
    Hardware::STUSB4500::Status initialStatus{};
    if (mSTUSB4500->readStatus(initialStatus)) {
        OS::MutexCS cs(mDataMutex);
        mSTUSB4500Status = initialStatus;
    } else {
        LOG_WARNING("failed to seed STUSB4500 status after init\n");
    }

    armExti();
    return true;
}

void UsbProtection::armExti()
{
    if (mIsArmed) {
        return;
    }

    mUsbFaultExti->init(Interface::IGpi::Pull::UP, Interface::IExti::Edge::RISING_FALLING, 5);
    mUsbFaultExti->setExtiHandler(this);
    mUsbFaultExti->clearInt();
    mUsbFaultExti->enableInt(true);

    mIsArmed = true;
}

void UsbProtection::disarmExti()
{
    if (!mIsArmed) {
        return;
    }

    mUsbFaultExti->enableInt(false);
    mUsbFaultExti->clearInt();
    mUsbFaultExti->setExtiHandler(nullptr);

    mIsArmed = false;
}

bool UsbProtection::isSTUSB4500Present()
{
    if (!mIsConfigured) {
        LOG_WARNING("component not configured\n");
        return false;
    }

    return mSTUSB4500->isPresent();
}

bool UsbProtection::isConnected()
{
    OS::MutexCS cs(mDataMutex);

    const auto attached = mSTUSB4500Status.portStatus1.b.ATTACHED_DEVICE;

    return attached == Hardware::STUSB4500::PortStatusAttachedDevice::SNK_ATT ||
           attached == Hardware::STUSB4500::PortStatusAttachedDevice::DBG_ATT;
}

bool UsbProtection::isSink()
{
    OS::MutexCS cs(mDataMutex);

    return mSTUSB4500Status.portStatus1.b.ATTACHED_DEVICE == Hardware::STUSB4500::PortStatusAttachedDevice::SNK_ATT;
}

bool UsbProtection::isDAM()
{
    OS::MutexCS cs(mDataMutex);

    return mSTUSB4500Status.portStatus1.b.ATTACHED_DEVICE == Hardware::STUSB4500::PortStatusAttachedDevice::DBG_ATT;
}

bool UsbProtection::isVBusFault()
{
    OS::MutexCS cs(mDataMutex);

    // Only treat VBUS_HIGH_STATUS (over-voltage > ~22V) as a real fault.
    // VBUS_LOW_STATUS is asserted during the charger ramp-up and on
    // disconnect, which would otherwise produce false-positive faults
    // on every plug event.
    return mSTUSB4500Status.typecMonitoringStatus0.b.VBUS_HIGH_STATUS;
}

bool UsbProtection::isCCFault()
{
    OS::MutexCS cs(mDataMutex);

    // VPU_VALID is a readiness indicator, not a fault: it is 0 until
    // the chip has established the pull-up after Type-C attach, so
    // reading it right after onUsbConnected() would yield a false-
    // positive fault. Only the VPU_OVP_FAULT bit indicates an actual
    // over-voltage on CC.
    return mSTUSB4500Status.ccHWFaultStatus1.b.VPU_OVP_FAULT;
}

bool UsbProtection::isDischargeFault()
{
    OS::MutexCS cs(mDataMutex);

    return mSTUSB4500Status.ccHWFaultStatus1.b.VBUS_DISCH_FAULT;
}

bool UsbProtection::interruptPowerFaultHandler(UsbProtection::PowerFaultStatus& status)
{
    if (getMode() != Mode::Active) {
        return false;
    }

    // STUSB powers from VBUS, so a normal cable removal leaves it dead.
    // Probe with a single no-retry transaction so we skip silently
    // instead of generating a readReg retry-fail log storm.
    if (!mSTUSB4500->isReady()) {
        return false;
    }

    Hardware::STUSB4500::Alerts stusbAlerts;
    if (!mSTUSB4500->readAlerts(stusbAlerts)) {
        LOG_ERROR("failed to read STUSB4500 alerts\n");
        countI2CError();
        return false;
    }

    // Use the existing cached status as the basis: readStatus(status, alerts)
    // only updates registers whose alert bit is set. If we passed a fresh
    // zero-initialized struct, every field outside the current alert set
    // would be wiped to zero on each call (e.g. PORT_STATUS_1 would lose
    // ATTACHED_DEVICE between events).
    Hardware::STUSB4500::Status stusbStatus;
    {
        OS::MutexCS cs(mDataMutex);
        stusbStatus = mSTUSB4500Status;
    }

    if (!mSTUSB4500->readStatus(stusbStatus, stusbAlerts)) {
        LOG_ERROR("failed to read STUSB4500 status\n");
        countI2CError();
        return false;
    }

    {
        OS::MutexCS cs(mDataMutex);
        mSTUSB4500Status = stusbStatus;
    }

    status.vbusFault  = isVBusFault();
    status.ccFault    = isCCFault();
    status.dischFault = isDischargeFault();

    return true;
}

bool UsbProtection::resetPdNegotiation()
{
    if (getMode() != Mode::Active) {
        return false;
    }

    return mSTUSB4500->swResetByReg();
}

void UsbProtection::quenchStusbAlerts()
{
    if (getMode() != Mode::Active) {
        return;
    }

    // Only touch the chip if it actually responds. A real unplug leaves
    // STUSB dead (no ACK) -> skip silently and let waitBusFree() recover
    // the bus. A standing fault on a powered cable keeps STUSB alive ->
    // masking all alerts releases the ALERT pin and frees PWR_INT_L.
    if (!mSTUSB4500->isReady()) {
        return;
    }

    mSTUSB4500->init(0);   // mask=0 -> writeReg writes ~0 = 0xFF (all masked)
}

bool UsbProtection::isUsbFault()
{
    if (getMode() != Mode::Active || !mIsArmed) {
        return false;
    }

    // USB_FAULT_L is active-low. If the line is currently low, the fault
    // is asserted right now. Otherwise check whether a brief pulse was
    // latched in the ISR since the previous call and consume it.
    if (mUsbFaultExti->get() == false) {
        return true;
    }

    return mFaultLatched.exchange(false, std::memory_order_acq_rel);
}

bool UsbProtection::enableDAMLines(bool status)
{
    if (getMode() != Mode::Active) {
        return false;
    }

    mDamEnablePin->set(status);

    LOG_DEBUG("DAM enable line - %s\n", status ? "ON" : "OFF");

    return true;
}

bool UsbProtection::isDAMLinesEnabled()
{
    if (getMode() != Mode::Active) {
        return false;
    }

    return mDamEnablePin->get();
}

bool UsbProtection::getStatus(Hardware::STUSB4500::Status& status)
{
    if (getMode() != Mode::Active) {
        return false;
    }

    if (!mSTUSB4500->readStatus(status)) {
        LOG_ERROR("failed to read STUSB4500 status\n");
        return false;
    }

    return true;
}

void UsbProtection::setUsbFaultIrqHandler(UsbFaultIrqHandler* handler)
{
    LOG_DEBUG("new USB fault handler = 0x%08X\n", handler);

    mUsbFaultIrqHandler.store(handler, std::memory_order_release);
}

void UsbProtection::extiHandler(Interface::IExti*      exti,
                                Interface::IExti::Edge edge)
{
    // ISR context: NO mutex, NO blocking calls.
    // Latch the fault if the line is currently low so a brief pulse
    // shorter than the IRQ-to-thread hop is not lost. Reading the GPIO
    // input register is a single MMIO load and is ISR-safe.
    if (mUsbFaultExti->get() == false) {
        mFaultLatched.store(true, std::memory_order_release);
    }

    auto* handler = mUsbFaultIrqHandler.load(std::memory_order_acquire);
    if (handler != nullptr) {
        handler->onUsbFaultIrq();
    }
}

void UsbProtection::countI2CError()
{
    OS::MutexCS cs(mDataMutex);

    if (mMode != Mode::Active) {
        return;
    }

    if (++mI2CErrorsCount >= kMaxI2CErrorsCount) {
        mMode = Mode::Bypassed;
    }
}
