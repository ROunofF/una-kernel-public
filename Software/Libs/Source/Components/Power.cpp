/**
 ******************************************************************************
 * @file    Power.cpp
 * @brief   Device power management.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Power.hpp"

#define LOG_MODULE_PRX      "Power"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include "System.hpp"
#include "Pmic.hpp"
#include "FuelGauge.hpp"
#include "Gpio.hpp"
#include "SwTimer.hpp"
#include "Rtc.hpp"
#include "Loggers/SystemLogger.hpp"

void Power::run()
{
    if (!mThread.init("Power", this, &Power::thread, osPriorityNormal, 5000)) {
        LOG_ERROR("mThread.init failed.\n");
    }
}

void Power::stop()
{
    mThread.deinit();
}

bool Power::setChargingInhibited(bool inhibit)
{
    // The PMIC write is a read-modify-write over I2C and can fail. Report that
    // rather than swallow it: this is the action a protection rule relies on,
    // and the caller has to know it did not stick. Nothing is published until
    // the hardware has taken it, otherwise the screen and the phone would show
    // charging as stopped while it is still running.
    if (!Pmic::GetInstance().setChargeEnable(!inhibit)) {
        LOG_ERROR("Charging %s FAILED\n", inhibit ? "inhibit" : "arm");
        return false;
    }

    if (mChargingInhibited != inhibit) {
        mChargingInhibited = inhibit;
        LOG_INFO("Charging %s\n", inhibit ? "inhibited" : "armed");
        mObserver.notify(&StatusCallback::onChargingInhibited, inhibit);
    }

    return true;
}

void Power::powerOff()
{
    Cmd cmd {Cmd::Id::POWER_OFF};
    sendToQueue(cmd, 100);

    while (true) {
        OS::Delay(1000);
    }
}

void Power::reboot()
{
    Cmd cmd {Cmd::Id::REBOOT};
    sendToQueue(cmd, 100);

    while (true) {
        OS::Delay(1000);
    }
}

void Power::onIrq()
{
    // ISR context. Coalesce: only enqueue when no IRQ_PWR_LINE is already
    // pending. Avoids burst from USB-C contact bounce flooding the queue.
    if (!mPwrLineDirty.exchange(true, std::memory_order_acq_rel)) {
        Cmd cmd {Cmd::Id::IRQ_PWR_LINE};
        sendToQueue(cmd, 0);
    }
}

void Power::onUsbFaultIrq()
{
    // ISR context. Coalesce IRQ_USB_LINE similarly to onIrq().
    if (!mUsbLineDirty.exchange(true, std::memory_order_acq_rel)) {
        Cmd cmd {Cmd::Id::IRQ_USB_LINE};
        sendToQueue(cmd, 0);
    }
}

void Power::lowBattTimHandler(void *arg)
{
    Cmd cmd {Cmd::Id::POWER_LOW_BATTERY_TIMEOUT};
    sendToQueue(cmd, 100);
}

void Power::fuelGaugeTimHandler(void *arg)
{
    // Periodic timer context. Coalesce so back-to-back fires (e.g. when
    // the Power thread was blocked on bus-free wait) collapse to one poll.
    if (!mFuelGaugePollDirty.exchange(true, std::memory_order_acq_rel)) {
        Cmd cmd {Cmd::Id::FUELGAUGE_POLL};
        sendToQueue(cmd, 100);
    }
}

void Power::fuelGaugeHandler()
{
    static int32_t sNotifyLevel = UINT32_MAX;
    static bool sLowBatteryNotified = false;

    // Read the whole gauge in one block. getTelemetry() returns false when the
    // gauge is absent or the I2C read fails - both of which the per-register
    // getters would instead report as a bogus 0, which then reads as an empty
    // battery and arms the power-off. Skip the cycle on a bad read and keep the
    // last known level for the UI; a valid reading follows on the next poll.
    Hardware::MAX17262::Telemetry t;
    if (!FuelGauge::GetInstance().getTelemetry(t)) {
        return;
    }

    mBattLevel = (t.repSoc > 100.0f) ? 100.0f : t.repSoc;

    // Publish the die temperature every cycle (unthrottled): the thermal guard
    // debounces it, and this read is already validated above at no extra I2C
    // cost.
    mObserver.notify(&StatusCallback::onBatteryTemperature, t.temp);

    // Notify observers only when the integer percent changes, to throttle the
    // UI updates.
    const int32_t level = static_cast<int32_t>(mBattLevel);
    if (sNotifyLevel != level) {
        sNotifyLevel = level;

        LOG_DEBUG("onBatteryLevelChanged %.2f\n", mBattLevel);
        mObserver.notify(&StatusCallback::onBatteryLevelChanged, mBattLevel);
    }

    // Power-off trigger, evaluated every call (not gated by the notify-throttle
    // above). Fires when SOC drops below skLowBatterySoc, or - as a safety net
    // for a gauge whose SOC stalls and never falls - when the cell voltage
    // drops to the critical floor. The averaged voltage is used here, not the
    // instantaneous one: a short load spike sags VCell momentarily and would
    // otherwise trip the floor and shut the watch off with charge to spare.
    // See those constants for the thresholds and rationale. Both readings come
    // from the validated snapshot above, so neither can be a stray 0.
    const float avgVCell = t.avgVCell;
    const bool socLow          = (mBattLevel < skLowBatterySoc);
    const bool voltageCritical = (avgVCell > skMinValidVoltage
                                  && avgVCell <= skCriticalShutdownVoltage);

    if ((socLow || voltageCritical) &&
            !sLowBatteryNotified &&
            !mIsUsbDetected) // No point powering off while running on USB.
    {
        sLowBatteryNotified = true;
        LOG_WARNING("onBatteryLow (SOC %.2f%%, AvgVCell %.3f V)\n", mBattLevel, avgVCell);

        // Point of no return. Notifying observers begins an orderly,
        // irreversible shutdown of the whole system; the timer below is only a
        // backstop that forces the power-off if that staged teardown stalls,
        // and is deliberately never cancelled or re-evaluated. Every validity
        // check therefore has to happen above this line - once notified, there
        // is no way back.
        mObserver.notify(&StatusCallback::onBatteryLow);
        mLowBattTimer.start(skLowBatteryPowerOffTimeout);
    }
}

void Power::pmicHandler()
{
    Pmic::GetInstance().clearIrq();     // acknowledge / clear PMIC IRQ flags
    const bool isUsb      = Pmic::GetInstance().isUsbDetected();
    const bool isCharging = Pmic::GetInstance().isCharging();

    auto& usbProtection = UsbProtection::GetInstance();

    if (mIsUsbDetected != isUsb) {
        mIsUsbDetected = isUsb;

        if (mIsUsbDetected) {
            // --- Attach. Classify the accessory BEFORE the system is told
            //     anything: a DAM (Type-C debug-accessory) attach must never
            //     surface as a USB connection. onUsbConnected() waits for the
            //     STUSB4500 to power up; usbProtectionHandler() then reads the
            //     DAM/fault state, latches mDamLatched on DAM, and calls
            //     publishUsbState(). Visibility is therefore decided only
            //     after classification, so a DAM cable produces no spurious
            //     connect/disconnect flicker for the system. ---
            usbProtection.onUsbConnected();
            usbProtectionHandler();
        } else {
            // --- Detach. We deliberately do NOT poll STUSB4500 here. On
            //     disconnect VBUS collapses and STUSB loses power within
            //     milliseconds; it still ACKs a quick address probe (VDD held
            //     by the VBUS_CON caps) but dies mid multi-byte read, leaving
            //     SDA stuck and tripping the next PCA9420 transaction on the
            //     shared bus. Letting the chip die untouched lets
            //     I2cRtos::waitBusFree() recover the bus for the PMIC. Any
            //     fault that caused this disconnect was already captured by
            //     usbProtectionHandler while the link was alive. ---
            usbProtection.onUsbDisconnected();

            // STUSB is now unpowered; its cached state is meaningless. Drop
            // the DAM line, release the DAM visibility latch, and clear the
            // fault so the next plug cycle starts from a clean state.
            if (mUsbDAM) {
                mUsbDAM = false;
                usbProtection.enableDAMLines(false);
                // Notify observers before this Kernel-prefixed line so an
                // onUsbDam observer that gates kernel logging re-enables it
                // first and the line is not swallowed.
                mObserver.notify(&StatusCallback::onUsbDam, false);
                LOG_INFO("USB DAM deasserted (cable removed)\n");
            }
            mDamLatched = false;
            mUsbFault   = false;

            // Start the next cable session without a remembered healthy
            // state, so a fault present right at attach is treated as
            // permanent (no STUSB reset attempt).
            mUsbHealthySeen = false;
        }
    }

    // Charging is internal truth (PMIC reports false once VBUS is gone).
    // publishUsbState() then mirrors USB/charging/fault to the system, or
    // hides them entirely while a DAM session is latched.
    mIsCharging = isCharging;
    publishUsbState();
}

void Power::publishUsbState()
{
    // The system sees USB only when it is physically present AND the cable
    // session was not classified as a Type-C debug accessory (see mDamLatched).
    const bool visible = mIsUsbDetected && !mDamLatched;

    if (visible && !mUsbVisible) {
        mUsbVisible = true;
        LOG_DEBUG("onUsbDetected 1\n");
        mObserver.notify(&StatusCallback::onUsbDetected, true);
    } else if (!visible && mUsbVisible) {
        // Visibility ended - a physical unplug, or a DAM session that latched
        // mid-cable. Either way mirror a clean disconnect to the system in the
        // usual order: charging off, fault off, then detached.
        if (mPubCharging) {
            mPubCharging = false;
            LOG_DEBUG("onCharging 0\n");
            mObserver.notify(&StatusCallback::onCharging, false);
        }
        if (mPubFault) {
            mPubFault = false;
            LOG_INFO("USB Protection fault cleared (USB no longer visible)\n");
            mObserver.notify(&StatusCallback::onUsbError, false);
        }
        mUsbVisible = false;
        LOG_DEBUG("onUsbDetected 0\n");
        mObserver.notify(&StatusCallback::onUsbDetected, false);
        return;
    }

    if (!mUsbVisible) {
        return;
    }

    // Visible: keep charging / fault mirrored to the system.
    if (mPubCharging != mIsCharging) {
        mPubCharging = mIsCharging;
        LOG_DEBUG("onCharging %d\n", mPubCharging);
        mObserver.notify(&StatusCallback::onCharging, mPubCharging);
    }
    if (mPubFault != mUsbFault) {
        mPubFault = mUsbFault;
        mObserver.notify(&StatusCallback::onUsbError, mPubFault);
    }
}

void Power::usbProtectionHandler()
{
    auto& usbProtection = UsbProtection::GetInstance();

    if (usbProtection.getMode() != UsbProtection::Mode::Active) {
        return;
    }

    if (!mIsUsbDetected) {
        // STUSB is unpowered, can't talk to it. Reset / DAM state cleared
        // in pmicHandler on the disconnect transition.
        return;
    }

    // Drain STUSB4500 alerts (releases PWR_INT_L) and capture fault flags.
    UsbProtection::PowerFaultStatus status{};
    if (!usbProtection.interruptPowerFaultHandler(status)) {
        return;
    }

    const bool lineFault = usbProtection.isUsbFault();
    const bool fault     = status.any() || lineFault;

    // Remember that this cable session reached a clean state at least once.
    if (!fault) {
        mUsbHealthySeen = true;
    }

    if (mUsbFault != fault) {
        mUsbFault = fault;

        if (mUsbFault) {
            LOG_INFO("USB Protection fault: vbus=%d cc=%d disch=%d nx20p=%d\n",
                     status.vbusFault,
                     status.ccFault,
                     status.dischFault,
                     lineFault);

            if (mUsbHealthySeen) {
                // The link worked before this fault, so it is most likely
                // a transient glitch. A NX20P0407 auto-recovery restores
                // the data switches but the USB host has already dropped
                // enumeration. Soft-reset STUSB4500: its VBUS_EN_SNK goes
                // Hi-Z, VBUS to the system drops, and PMIC sees a clean
                // disconnect/reconnect cycle that makes the host
                // re-enumerate. If the fault cause persists, the chip
                // re-attaches into a fault-at-start state (see below) and
                // we stop kicking it, breaking any reset loop.
                LOG_INFO("Recoverable fault, resetting STUSB PD negotiation\n");
                usbProtection.resetPdNegotiation();
            } else {
                // Fault present from the very start of the session: most
                // likely a permanent condition (bad cable, standing short
                // to VBUS). Resetting would only churn, so leave it for
                // the user to unplug.
                LOG_INFO("Fault present at attach, please unplug the cable\n");
            }
        } else {
            LOG_INFO("USB Protection fault cleared\n");
        }
    }

    // DAM detection. The STUSB classifies a Type-C debug accessory as
    // DBG_ATT; the first such report latches the session as DAM so USB stays
    // hidden from the system until unplug (see mDamLatched / publishUsbState).
    // Gated by UNA_DAM_HIDE_USB so a production build can keep USB visible.
#if UNA_DAM_HIDE_USB
    if (usbProtection.isDAM()) {
        mDamLatched = true;
    }
#endif

    // DAM line control is fully automatic here, no upper-layer action
    // required. A fault forces the DAM data path off; otherwise the path
    // tracks the chip-reported DAM state. onUsbDam reports the transition to
    // observers and stays truthful even while DAM hides USB from the system.
    const bool wantDAM = !mUsbFault && usbProtection.isDAM();
    if (mUsbDAM != wantDAM) {
        mUsbDAM = wantDAM;
        usbProtection.enableDAMLines(mUsbDAM);
        // Notify observers before emitting this Kernel-prefixed line: an
        // onUsbDam observer may switch kernel logging on/off by DAM state, so
        // logging after the notify makes this line follow the same rule
        // (suppressed on assert, shown on deassert).
        mObserver.notify(&StatusCallback::onUsbDam, mUsbDAM);
        LOG_INFO("USB DAM %s\n", mUsbDAM ? "asserted" : "deasserted");
    }

    // Reconcile the system-facing view: a freshly latched DAM session replays
    // a clean disconnect here, and fault transitions (while visible) are
    // forwarded to onUsbError.
    publishUsbState();
}

Power::Power()
{
    mCmdQueue.init(LOG_MODULE_PRX);
    mLowBattTimer.init(LOG_MODULE_PRX, this, &Power::lowBattTimHandler,    false, nullptr);
    mFuelGaugeTimer.init(LOG_MODULE_PRX, this, &Power::fuelGaugeTimHandler, true,  nullptr);
}

void Power::thread()
{
    // Make sure that components are initialized
    FuelGauge::GetInstance().init();

    Pmic& pmic = Pmic::GetInstance();

    pmic.init();
    pmic.setIrqHandler(this);

    // Register the USB-fault handler once, before pmicHandler() can trigger
    // UsbProtection::onUsbConnected() and arm the EXTI line.
    UsbProtection::GetInstance().setUsbFaultIrqHandler(this);

    mFuelGaugeTimer.start(skFuelGaugeServicePeriod);

    LOG_DEBUG("Run\n");

    // Get current state. pmicHandler() runs USB-detect logic and itself
    // calls usbProtectionHandler() on a connect transition, so the
    // initial drain on cold-boot-with-USB is covered too.
    pmicHandler();
    fuelGaugeHandler();

    // Release run() only when the state is fully synchronized
    mThread.setInitialized();

    // Register StreamBuffer for automatic interruption
    OS::CancellableGuard guard(&mThread, &mCmdQueue);

    // Main loop
    while (!mThread.shouldStop()) {

        // Wait for command (blocks until available)
        Cmd cmd {};
        mCmdQueue.pop(cmd);

        if (mThread.shouldStop()) {
            LOG_DEBUG("Interrupted\n");
            break;
        }

        LOG_DEBUG("cmd id=%u\n", cmd.id);

        switch (cmd.id) {
            case Cmd::Id::IRQ_PWR_LINE: {
                hIrqPwrLine();
            } break;

            case Cmd::Id::IRQ_USB_LINE: {
                hIrqUsbLine();
            } break;

            case Cmd::Id::POWER_OFF: {
                hPowerOff();
            } break;

            case Cmd::Id::REBOOT: {
                hReboot();
            } break;

            case Cmd::Id::FUELGAUGE_POLL: {
                hFuelGaugePoll();
            } break;

            case Cmd::Id::POWER_LOW_BATTERY_TIMEOUT: {
                hLowBatteryTimeout();
            } break;

            case Cmd::Id::NONE:
            default:
                break;
        }
    }

    Pmic::GetInstance().setIrqHandler(nullptr);
    UsbProtection::GetInstance().setUsbFaultIrqHandler(nullptr);
    UsbProtection::GetInstance().onUsbDisconnected();

    mLowBattTimer.stop();
    mFuelGaugeTimer.stop();

    LOG_DEBUG("Stop\n");
}

bool Power::sendToQueue(const Cmd& cmd, uint32_t timeout)
{
    if (!mCmdQueue.push(cmd, timeout)) {
        LOG_ERROR("Failed to send cmd id=%u to the queue. timeout %u ms\n", cmd.id, timeout);
        return false;
    }

    return true;
}

void Power::hIrqPwrLine()
{
    auto& pmic = Pmic::GetInstance();

    constexpr uint8_t kMaxAttempts        = 3;
    constexpr uint32_t kInterAttemptDelay = 20;   // ms

    // Release the coalescing flag BEFORE reading the chip. Any IRQ that
    // arrives from now on enqueues exactly one follow-up command, so we
    // never miss an edge that occurred during processing.
    mPwrLineDirty.store(false, std::memory_order_release);

    // PWR_INT_L is wired-OR of PMIC INTB and STUSB4500 ALERT. Service PMIC
    // first (it owns the USB-detect state and may have triggered the IRQ
    // on its own), then drain STUSB alerts so it releases the line.
    //
    // STUSB die-out is handled at the I2C layer by I2cRtos::waitBusFree(),
    // so no extra ms-level delay is needed here.
    for (uint8_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) {
            OS::Delay(kInterAttemptDelay);
        }

        pmicHandler();

        if (!pmic.isIrqAsserted()) {
            return;
        }

        if (mIsUsbDetected) {
            usbProtectionHandler();
        } else {
            // PMIC reports no USB, yet the line is still asserted. STUSB
            // can still be alive on connector VDD and holding ALERT due to
            // a standing fault (e.g. CC OVP keeps VBUS_EN_SNK Hi-Z, so PMIC
            // sees no VBUS). Retry masking its alerts to release the line.
            UsbProtection::GetInstance().quenchStusbAlerts();
        }

        if (!pmic.isIrqAsserted()) {
            return;
        }
    }

    LOG_ERROR("failed to handle PWR_LINE interrupt\n");
}

void Power::hIrqUsbLine()
{
    // Release coalescing flag (same pattern as hIrqPwrLine).
    mUsbLineDirty.store(false, std::memory_order_release);

    // NX20P0407 FAULT pulse was captured by UsbProtection::extiHandler
    // (latched in mFaultLatched). Route through usbProtectionHandler to
    // also pick up any STUSB alerts that may have fired in parallel,
    // and let isUsbFault() consume the latch.
    usbProtectionHandler();
}

void Power::hPowerOff()
{
    LOG_DEBUG("Power Off\n");
    mObserver.notify(&StatusCallback::onPowerOff);

    // Waiting for the user to release the button
    Driver::SwTimer t(100);
    while (!t.expired()) {
        OS::Delay(10);
        if (!Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_ON_1V8_L).get()) {
            t.reset();
        }
    }

    // Save flag to use long press for power on
    Driver::Rtc::GetInstance().writeBackupReg(RTC_BRK_REG_POWER_OFF_FLAG, 1);

    // Flush the system log to flash and close it cleanly before power is cut.
    if (Component::SystemLogger::isStarted()) {
        Component::SystemLogger::GetInstance().stop();
    }

    Pmic::GetInstance().powerOff();

    while(true) {
        OS::Delay(10);
        LOG_ERROR("Still alive!\n");
        Pmic::GetInstance().powerOff();
    }
}

void Power::hReboot()
{
    LOG_DEBUG("Reboot\n");
    mObserver.notify(&StatusCallback::onPowerOff);

    OS::Delay(10);

    Driver::System::Reset();
}

void Power::hFuelGaugePoll()
{
    // Release coalescing flag (same pattern as hIrqPwrLine).
    mFuelGaugePollDirty.store(false, std::memory_order_release);

    fuelGaugeHandler();
}

void Power::hLowBatteryTimeout()
{
    LOG_DEBUG("Low Battery Timeout\n");
    hPowerOff();    // just power off
}
