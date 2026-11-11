/**
 ******************************************************************************
 * @file    ErrorMonitor.hpp
 * @brief   Monitoring system errors.
 ******************************************************************************
 *
 *          Owns the error conditions and what the single error screen shows.
 *          Three things are deliberately kept apart:
 *
 *            condition - a real hazard flag, set and cleared only by its
 *                        producer (USB protection, the charge thermal guard,
 *                        the cable and voltage checks). It keeps enforcing
 *                        whatever it blocks, e.g. charging stays inhibited.
 *            display   - the one error screen, arbitrated from the active
 *                        conditions by priority.
 *            acknowledge - the user hiding the display. It never clears a
 *                        condition, so hiding the screen fixes nothing.
 *
 *          Because acknowledging changes nothing physical, the display is
 *          raised again when the user retries the blocked action - a cable
 *          plug. Staying silent there would read as "the retry worked" while
 *          charging is still inhibited. A full recovery resets the
 *          acknowledgement, so the next episode starts clean.
 *
 *          Only a still-active condition can be re-shown, which bounds how long
 *          a plug keeps re-raising it: a disconnect long enough for the thermal
 *          guard to clear its rules is a recovery, so the plug after it is a
 *          clean start and stays silent until something trips again.
 *
 ******************************************************************************
 */

#ifndef __ERROR_MONITOR_HPP
#define __ERROR_MONITOR_HPP

#include <variant>

#include "SystemError.hpp"
#include "ErrorArbiter.hpp"
#include "Power.hpp"
#include "OS.hpp"
#include "Observer.hpp"
#include "ChargeThermalMonitor.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"



class ErrorMonitor : public Power::StatusCallback,
                     public SDK::Interface::ISensorDataListener,
                     public IThermalActions {

    ErrorArbiter mArbiter;

public:

    /**
     * @brief   Error Callbacks interface
     */
    class Callback {
    public:

        /**
         * @brief   Called when an error condition becomes active (a real
         *          transition). For consumers that act on the condition itself,
         *          e.g. tearing the USB stack down on USB_PROTECTION.
         * @param   error: The error condition that became active.
         */
        virtual void onSystemErrorSet(SystemError error) {}

        /**
         * @brief   Called when an error condition clears.
         * @param   error: The error condition that cleared.
         */
        virtual void onSystemErrorClear(SystemError error) {}

        /**
         * @brief   Called when the arbitrated error to display changes. For the
         *          single error screen.
         * @param   code: The error to show, or SystemError::NONE to leave the
         *                error screen.
         */
        virtual void onSystemErrorDisplay(SystemError code) {}

        /**
         * @brief   Called when the most urgent ACTIVE condition changes,
         *          regardless of what the screen shows or what the user
         *          acknowledged. For consumers outside the watch - the phone -
         *          which care about the hazard, not about the local UI.
         * @param   code: The active condition, or SystemError::NONE once
         *                everything has recovered.
         */
        virtual void onSystemErrorCondition(SystemError code) {}

    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~Callback() = default;
    };

    /**
     * @brief   Get ErrorMonitor instance.
     * @retval  ErrorMonitor instance.
     */
    static ErrorMonitor& GetInstance()
    {
        static ErrorMonitor sInstance;
        return sInstance;
    }

    /**
     * @brief   Initialize ErrorMonitor module.
     */
    void run();

    /**
     * @brief   Stop ErrorMonitor module.
     */
    void stop();


    void attachStatusCallback(Callback *pCallback)
    {
        mObserver.add(pCallback);
    }

    void detachStatusCallback(Callback *pCallback)
    {
        mObserver.del(pCallback);
    }


    void set(SystemError e);
    void clear(SystemError e);
    void clearAll();

    /// User dismissed the current error screen (routed from the GUI).
    void acknowledge();

    /// Re-announce the current condition even though it did not change, for a
    /// consumer that just started listening (a phone subscribing to
    /// notifications). Only queues the request: the answer is produced on this
    /// module's own thread, so the caller never touches the arbiter and never
    /// carries the reply out of its own callback.
    void requestConditionResend();

    bool isSet(SystemError e) const { return mArbiter.isActive(e); }
    bool hasAny() const          { return mArbiter.anyActive(); }
    bool hasNone() const         { return !mArbiter.anyActive(); }
    size_t count() const         { return mArbiter.activeCount(); }

private:

    bool mIsInit = false;
    bool mIsRun = false;

    // Last display pushed to observers; only changes are notified.
    SystemError mShownDisplay = SystemError::NONE;

    // Same, for the condition channel. Tracked separately because the two move
    // independently: acknowledging changes the display but not the condition.
    SystemError mShownCondition = SystemError::NONE;

    // Array of an observers
    Observer::Holder<Callback, 5> mObserver;

    struct Events {
        using None = std::monostate;
        struct Start {};
        struct Stop {};
        struct Set {SystemError e;};
        struct Clear {SystemError e;};
        struct ClearAll {};
        struct UsbCable {bool state; uint32_t ts;};
        struct UsbError {bool active;};
        struct DieTemp {float degC;};
        struct Ambient {float degC;};
        struct ShutdownExpired {};
        struct Acknowledge {};
        struct ResendCondition {};

        using QData = std::variant<
                None,
                Start,
                Stop,
                Set,
                Clear,
                ClearAll,
                UsbCable,
                UsbError,
                DieTemp,
                Ambient,
                ShutdownExpired,
                Acknowledge,
                ResendCondition
                >;
    };

    OS::Queue<Events::QData, 5> mQueue;

    // Observer callbacks run on this thread, and they reach deep: the USB stack
    // is torn down here on a protection fault, and the error state is pushed to
    // the phone through the BLE stack. Both need far more than a minimal stack.
    OS::Thread<ErrorMonitor, 4*1024> mThread;


    struct CableConnectionCtx {
        static constexpr uint32_t skMaxEvents = 4;
        static constexpr uint32_t skWindowsMs = 10000;
        uint32_t mTimestamps[skMaxEvents] {};
        uint32_t mIndex = 0;
        bool isTooFrequent(uint32_t ms);
    } mCableConnection;


    /**
     * @brief   Constructor.
     */
    ErrorMonitor()
        : mThermal(*this)
        , mAmbientConn(SDK::Sensor::Type::AMBIENT_TEMPERATURE, this,
                       skAmbientPeriodMs, /*latency*/ 0u)
    {
    }

    /**
     * @brief   Destructor.
     */
    virtual ~ErrorMonitor() = default;

    // Ambient temperature sampling rate while USB is present (ms).
    static constexpr float skAmbientPeriodMs = 1000.0f;

    // Charge-time over-temperature guard (pure logic) + its ambient feed.
    ChargeThermalMonitor mThermal;
    Sensor::Connection   mAmbientConn;

    // Power
    virtual void onUsbDetected(bool state) override;
    virtual void onUsbError(bool active) override;
    virtual void onBatteryTemperature(float degC) override;

    // Sensor layer (ambient temperature stream)
    virtual void onSdlNewData(uint16_t handle, const SDK::Sensor::Data *base,
                              uint16_t count, uint16_t stride) override;

    // IThermalActions - routed to Power and the error screen.
    virtual bool inhibitCharging() override;
    virtual bool armCharging() override;
    virtual void raiseWarning(SystemError e) override;
    virtual void clearWarning(SystemError e) override;
    virtual void requestShutdown() override;
    virtual void cancelShutdown() override;

    // How long the over-temperature warning shows and buzzes before power-off.
    static constexpr uint32_t skShutdownBuzzMs = 10000u;

    // One-shot: fires the thermal power-off after the warning/buzz window.
    OS::Timer<ErrorMonitor> mShutdownTimer;
    void shutdownTimHandler(void *arg);

    /**
     * @brief   Endless thread.
     */
    void thread();

    /**
     * @brief   Mark an error condition active, then reconcile the display.
     */
    void setError(SystemError e);

    /**
     * @brief   Clear an error condition, then reconcile the display.
     */
    void clearError(SystemError e);

    /**
     * @brief   Clear all active error conditions.
     */
    void clearAllErrors();

    /**
     * @brief   Recompute the arbitrated display and notify observers on change.
     */
    void reconcileDisplay();

    // Event handlers (overloaded)
    void handle(const std::monostate&);
    void handle(const Events::Start&);
    void handle(const Events::Stop&);
    void handle(const Events::Set& event);
    void handle(const Events::Clear& event);
    void handle(const Events::ClearAll&);
    void handle(const Events::UsbCable& event);
    void handle(const Events::UsbError& event);
    void handle(const Events::DieTemp& event);
    void handle(const Events::Ambient& event);
    void handle(const Events::ShutdownExpired&);
    void handle(const Events::Acknowledge&);
    void handle(const Events::ResendCondition&);
};

#endif /* __ERROR_MONITOR_HPP */
