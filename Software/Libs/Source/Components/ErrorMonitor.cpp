/**
 ******************************************************************************
 * @file    ErrorMonitor.cpp
 * @brief   Monitoring system errors.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ErrorMonitor.hpp"

#define LOG_MODULE_PRX      "ErrorMonitor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ErrorMonitor::run()
{
    if (!mIsInit) {
        mQueue.init(LOG_MODULE_PRX);
        mThread.init(LOG_MODULE_PRX, this, &ErrorMonitor::thread, osPriorityNormal);
        mShutdownTimer.init(LOG_MODULE_PRX, this, &ErrorMonitor::shutdownTimHandler,
                            false, nullptr);

        mIsInit = true;
    }

    mQueue.push(Events::Start{});
}

void ErrorMonitor::stop()
{
    mQueue.push(Events::Stop{});
}

void ErrorMonitor::set(SystemError e)
{
    mQueue.push(Events::Set{e});
}

void ErrorMonitor::clear(SystemError e)
{
    mQueue.push(Events::Clear{e});
}

void ErrorMonitor::clearAll()
{
    mQueue.push(Events::ClearAll{});
}

void ErrorMonitor::acknowledge()
{
    mQueue.push(Events::Acknowledge{});
}

void ErrorMonitor::requestConditionResend()
{
    // Called from the BLE stack's event dispatch, which holds the BLE core
    // lock: blocking here would take the whole link down, so the request is
    // dropped if the queue is momentarily full. Losing it costs the subscriber
    // its initial value until the next condition change - not the link.
    if (!mQueue.push(Events::ResendCondition{}, 0)) {
        LOG_WARNING("Condition resend dropped: queue full\n");
    }
}

// ---------------------------------------------------------------------------
// Power callbacks
// ---------------------------------------------------------------------------

void ErrorMonitor::onUsbDetected(bool state)
{
    mQueue.push(Events::UsbCable{state, OS::GetTimeMs()});
}

void ErrorMonitor::onUsbError(bool active)
{
    mQueue.push(Events::UsbError{active});
}

void ErrorMonitor::onBatteryTemperature(float degC)
{
    mQueue.push(Events::DieTemp{degC});
}

// ---------------------------------------------------------------------------
// Sensor layer callback (ambient temperature stream)
// ---------------------------------------------------------------------------

void ErrorMonitor::onSdlNewData(uint16_t handle, const SDK::Sensor::Data *base,
                                uint16_t count, uint16_t stride)
{
    if (!mAmbientConn.matchesDriver(handle) || count == 0u) {
        return;
    }

    SDK::Sensor::DataBatch batch(base, count, stride);
    SDK::SensorDataParser::Temperature p(batch[0]);

    const float degC = p.getTemperature();
    if (degC <= -273.0f) {   // parser's invalid-sample sentinel
        return;
    }

    mQueue.push(Events::Ambient{degC});
}

// ---------------------------------------------------------------------------
// IThermalActions - side effects of the charge-thermal guard
// ---------------------------------------------------------------------------

bool ErrorMonitor::inhibitCharging()
{
    return Power::GetInstance().setChargingInhibited(true);
}

bool ErrorMonitor::armCharging()
{
    return Power::GetInstance().setChargingInhibited(false);
}

void ErrorMonitor::raiseWarning(SystemError e)
{
    setError(e);
}

void ErrorMonitor::clearWarning(SystemError e)
{
    clearError(e);
}

void ErrorMonitor::requestShutdown()
{
    // Show the over-temperature screen (which also buzzes), then power off
    // once the warning window elapses.
    setError(SystemError::TEMPERATURE_SHUTDOWN);
    mShutdownTimer.start(skShutdownBuzzMs);
}

void ErrorMonitor::cancelShutdown()
{
    // USB reconnected before the power-off ran - stop the timer and drop the
    // shutdown warning; the guard reconciles back to the critical warning.
    mShutdownTimer.stop();
    clearError(SystemError::TEMPERATURE_SHUTDOWN);
}

void ErrorMonitor::shutdownTimHandler(void *arg)
{
    (void)arg;
    // Marshal the power-off onto the ErrorMonitor thread rather than run it in
    // the timer service context.
    mQueue.push(Events::ShutdownExpired{});
}

// ---------------------------------------------------------------------------
// Helper methods
// ---------------------------------------------------------------------------

void ErrorMonitor::setError(SystemError e)
{
    if (!mArbiter.isActive(e)) {
        mArbiter.setActive(e);
        LOG_INFO("Set Err %u\n", static_cast<uint32_t>(e));
        mObserver.notify(&Callback::onSystemErrorSet, e);
    }
    reconcileDisplay();
}

void ErrorMonitor::clearError(SystemError e)
{
    if (mArbiter.isActive(e)) {
        mArbiter.clearActive(e);
        LOG_INFO("Clear Err %u\n", static_cast<uint32_t>(e));
        mObserver.notify(&Callback::onSystemErrorClear, e);
    }
    reconcileDisplay();
}

void ErrorMonitor::clearAllErrors()
{
    for (size_t i = 0; i < static_cast<size_t>(SystemError::COUNT); ++i) {
        const SystemError e = static_cast<SystemError>(i);
        if (mArbiter.isActive(e)) {
            mArbiter.clearActive(e);
            mObserver.notify(&Callback::onSystemErrorClear, e);
        }
    }
    reconcileDisplay();
}

void ErrorMonitor::reconcileDisplay()
{
    // The condition is reported first: it is the one that reflects the hazard,
    // and an observer acting on it should not see a stale value if showing the
    // screen has side effects.
    const SystemError condition = mArbiter.currentCondition();
    if (condition != mShownCondition) {
        mShownCondition = condition;
        mObserver.notify(&Callback::onSystemErrorCondition, condition);
    }

    const SystemError display = mArbiter.currentDisplay();
    if (display != mShownDisplay) {
        mShownDisplay = display;
        LOG_INFO("Display Err %u\n", static_cast<uint32_t>(display));
        mObserver.notify(&Callback::onSystemErrorDisplay, display);
    }
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ErrorMonitor::handle(const std::monostate&)
{
    // None - ignore
}

void ErrorMonitor::handle(const Events::Start&)
{
    if (mIsRun) {
        return;
    }

    Power& power = Power::GetInstance();
    power.attachStatusCallback(this);

    // Seed the thermal guard with the current USB state: if the watch booted
    // already on charge, no onUsbDetected edge is coming to start monitoring.
    if (power.isUsbDetected()) {
        mAmbientConn.connect();
        mThermal.setUsbPresent(true);
    }

    mIsRun = true;
    LOG_INFO("Started\n");
}

void ErrorMonitor::handle(const Events::Stop&)
{
    if (!mIsRun) {
        return;
    }

    Power& power = Power::GetInstance();
    power.detachStatusCallback(this);
    mAmbientConn.disconnect();
    mThermal.setUsbPresent(false);
    clearAllErrors();
    mIsRun = false;
    LOG_INFO("Stopped\n");
}

void ErrorMonitor::handle(const Events::Set& event)
{
    setError(event.e);
}

void ErrorMonitor::handle(const Events::Clear& event)
{
    clearError(event.e);
}

void ErrorMonitor::handle(const Events::ClearAll&)
{
    clearAllErrors();
}

void ErrorMonitor::handle(const Events::UsbCable& event)
{
    // Plugging the cable back in is the user retrying to charge. Anything the
    // user dismissed earlier still blocks that retry, so drop the
    // acknowledgement and say why again - silence would look like a fix.
    if (event.state) {
        mArbiter.resetAcknowledge();
    }

    if (event.state && mCableConnection.isTooFrequent(event.ts)) {
        setError(SystemError::CABLE_CONNECTION);
    }

    // Ambient temperature is only needed while charging is possible, and the
    // subscription is what powers the barometer - so connect on plug, drop on
    // unplug. The die feed (onBatteryTemperature) runs independently, so the
    // guard can still recover after an ambiguous disconnect.
    if (event.state) {
        mAmbientConn.connect();
    } else {
        mAmbientConn.disconnect();
    }
    mThermal.setUsbPresent(event.state);

    // The thermal guard holds its alert at the peak, so a still-latched
    // condition raises nothing here - reconcile explicitly to re-show it.
    if (event.state) {
        reconcileDisplay();
    }
}

void ErrorMonitor::handle(const Events::UsbError& event)
{
    if (event.active) {
        setError(SystemError::USB_PROTECTION);
    } else {
        clearError(SystemError::USB_PROTECTION);
    }
}

void ErrorMonitor::handle(const Events::DieTemp& event)
{
    mThermal.feedDie(event.degC);
}

void ErrorMonitor::handle(const Events::Ambient& event)
{
    mThermal.feedAmbient(event.degC);
}

void ErrorMonitor::handle(const Events::Acknowledge&)
{
    mArbiter.acknowledge();
    reconcileDisplay();
}

void ErrorMonitor::handle(const Events::ResendCondition&)
{
    // A listener that just joined needs the value even though it did not
    // change, so this deliberately bypasses the change gate in
    // reconcileDisplay(). Only the condition is re-announced - the display is
    // the watch's own screen, which never needed telling.
    const SystemError condition = mArbiter.currentCondition();
    mShownCondition = condition;
    mObserver.notify(&Callback::onSystemErrorCondition, condition);
}

void ErrorMonitor::handle(const Events::ShutdownExpired&)
{
    // Guard the race where USB is (re)connected just as the timer fires: a
    // power-off with valid VBUS does not stick and hangs. If USB is present,
    // abort back to the critical warning instead.
    if (Power::GetInstance().isUsbDetected()) {
        LOG_WARNING("Over-temperature power-off aborted: USB present\n");
        mThermal.setUsbPresent(true);
        return;
    }

    LOG_WARNING("Over-temperature power-off\n");
    Power::GetInstance().powerOff();
}

// ---------------------------------------------------------------------------
// Thread
// ---------------------------------------------------------------------------

void ErrorMonitor::thread()
{
    for (;;) {

        Events::QData data{};

        if (!mQueue.pop(data)) {
            continue;
        }

        std::visit([this](const auto& event) {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, Events::Start>) {
                handle(event);
            }  else {
                if (mIsRun) {
                    handle(event);
                }
            }
        }, data);
    }
}

// ---------------------------------------------------------------------------
// Cable connection frequency detection
// ---------------------------------------------------------------------------

bool ErrorMonitor::CableConnectionCtx::isTooFrequent(uint32_t ms)
{
    uint32_t oldest = mTimestamps[mIndex];
    mTimestamps[mIndex] = ms;
    mIndex = (mIndex + 1) % skMaxEvents;
    return (oldest != 0) && (ms - oldest < skWindowsMs);
}
