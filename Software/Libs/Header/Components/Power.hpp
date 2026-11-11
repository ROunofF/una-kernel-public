/**
 ******************************************************************************
 * @file    Power.hpp
 * @brief   Device power management.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __POWER_HPP
#define __POWER_HPP

#include "Porting.h"

#include <atomic>

#include "Pmic.hpp"
#include "OS.hpp"
#include "Observer.hpp"
#include "UsbProtection.hpp"

// Master switch for the DAM USB-masking feature. When 0, a DAM (Type-C
// debug-accessory) attach behaves like the pre-feature firmware: the debug
// pin is still enabled, but USB stays fully visible to the system (the COM
// port comes up normally) and the kernel-log filter is not installed. Build
// with this set to 0 for a test rig (e.g. a USB breakout board with extra CC
// pull-ups) that can be misread as a debug accessory.
#ifndef UNA_DAM_HIDE_USB
#define UNA_DAM_HIDE_USB 1
#endif

class Power : virtual public Pmic::IrqHandler,
              virtual public UsbProtection::UsbFaultIrqHandler
{

public:

    /**
     * @brief   Status Callbacks interface
     */
    class StatusCallback {
    public:
        /**
         * @brief   Called when the charging state changes.
         * @param   state: 'true' if charging is active, 'false' otherwise.
         */
        virtual void onCharging(bool state) {}

        /**
         * @brief   Called when charging is blocked or re-armed by a protection
         *          rule. Distinct from onCharging: that reports whether current
         *          actually flows (and so is also 'false' on a full battery),
         *          while this reports that the watch is refusing to charge.
         * @param   state: 'true' when charging is inhibited, 'false' when armed.
         */
        virtual void onChargingInhibited(bool state) {}

        /**
         * @brief   Called when a USB connection is detected or disconnected.
         * @param   state: 'true' if the USB is connected, 'false' if disconnected.
         */
        virtual void onUsbDetected(bool state) {}

        /**
         * @brief   Called when USB Debug Accessory Mode is asserted or
         *          deasserted. The DAM data lines are switched automatically
         *          by Power; Power itself takes no further action on this
         *          state, but unlike onUsbDetected/onCharging/onUsbError it
         *          stays truthful while DAM hides USB from the system, so
         *          observers may act on it (e.g. to gate kernel logging).
         * @param   active: 'true' if DAM is active, 'false' otherwise.
         */
        virtual void onUsbDam(bool active) {}

        /**
         * @brief   Called when the USB protection circuit (STUSB4500 /
         *          NX20P0407) asserts or clears a fault.
         * @param   active: 'true' when a fault is present, 'false' when
         *                  it is cleared.
         */
        virtual void onUsbError(bool active) {}

        /**
         * @brief   Called when the battery level changes by 1%.
         * @param   level: Current battery level as a percentage (0.0 - 100.0).
         */
        virtual void onBatteryLevelChanged(float level) {}

        /**
         * @brief   Reports the fuel-gauge die temperature each poll cycle.
         * @param   degC: Die temperature in degrees Celsius. This is the gauge
         *                die (near the USB connector), not the cell or ambient.
         */
        virtual void onBatteryTemperature(float degC) {}

        /**
         * @brief   Called when the battery is critically low. This begins an
         *          orderly, irreversible shutdown of the system; the power
         *          manager forces power-off 30 seconds later as a backstop if
         *          that teardown stalls.
         */
        virtual void onBatteryLow() {}

        /**
         * @brief   Called when the system is about to power off.
         *          This event triggers immediately before the shutdown process.
         */
        virtual void onPowerOff() {}

    protected:

        /**
         * @brief   Destructor.
         */
        virtual ~StatusCallback() = default;
    };

    /**
     * @brief   Get Power instance.
     * @retval  Power instance.
     */
    static Power& GetInstance()
    {
        static Power sInstance;
        return sInstance;
    }

    /**
     * @brief   Initialize Power module.
     */
    void run();

    /**
     * @brief   Stop Power module.
     */
    void stop();

    /**
     * @brief   Get actual battery level.
     * @retval  Actual battery level (0 - 100%).
     */
    float getBatteryLevel() const
    {
        return mBattLevel;
    }

    /**
     * @brief   Check if battery is charging.
     * @retval  'true' battery is charging, 'false' otherwise.
     */
    bool isCharging() const
    {
        return mUsbVisible && mIsCharging;
    }

    /**
     * @brief   Check if charging is being refused by a protection rule.
     * @retval  'true' charging is inhibited, 'false' it is armed.
     */
    bool isChargingInhibited() const
    {
        return mChargingInhibited;
    }

    /**
     * @brief   Check if USB detected.
     * @retval  'true' USB detected, 'false' otherwise.
     *
     * Reports the system-facing view: a Type-C debug-accessory (DAM)
     * session is hidden, so this returns 'false' while DAM is latched even
     * though VBUS is physically present. See mDamLatched.
     */
    bool isUsbDetected() const
    {
        return mUsbVisible;
    }

    /**
     * @brief   Check current USB protection fault state (for polling).
     * @retval  'true' a USB protection fault is active, 'false' otherwise.
     * @note    Hidden while a DAM session is latched (see isUsbDetected).
     */
    bool isUsbError() const
    {
        return mUsbVisible && mUsbFault;
    }

    /**
     * @brief   Check current USB Debug Accessory Mode state (for polling).
     * @retval  'true' DAM is active, 'false' otherwise.
     */
    bool isUsbDam() const
    {
        return mUsbDAM;
    }

    /**
     * @brief   Power OFF
     */
    [[noreturn]] void powerOff();

    /**
     * @brief   Reboot
     */
    [[noreturn]] void reboot();


    /**
     * @brief   Inhibit or re-enable the charge path.
     * @param   inhibit: 'true' stops charging, 'false' re-enables it.
     *
     * Reversible: toggles the PMIC charge-enable bit. Used by the charge-time
     * over-temperature guard to stop charging without powering the watch off.
     *
     * @retval  'true' the PMIC took it. 'false' means the charge path is
     *          unchanged - the bit is set over I2C and that can fail. The
     *          caller must treat a failure as "still charging" and try again;
     *          nothing is reported to observers unless the write succeeded.
     */
    bool setChargingInhibited(bool inhibit);

    void attachStatusCallback(StatusCallback *pCallback)
    {
        mObserver.add(pCallback);
    }

    void detachStatusCallback(StatusCallback *pCallback)
    {
        mObserver.del(pCallback);
    }

protected:

    /**
     * @brief   Constructor.
     */
    Power();

    /**
     * @brief   Destructor.
     */
    virtual ~Power() = default;

private:
    bool mUsbFault = false;
    bool mUsbDAM = false;

    // DAM (Type-C debug-accessory) visibility latch. Set as soon as the
    // STUSB4500 classifies the attachment as a debug accessory and held for
    // the rest of the cable session - cleared only on physical unplug. While
    // latched the watch still draws VBUS power and charges, but USB is hidden
    // from the system entirely: onUsbDetected/onCharging/onUsbError and the
    // matching getters all report "no USB". A DAM cable plugged from the
    // start is never announced; DAM appearing mid-session replays a clean
    // disconnect; DAM dropping mid-session keeps USB hidden until unplug.
    bool mDamLatched = false;

    // System-facing USB view, mirrored to observers and the public getters.
    // mUsbVisible == (mIsUsbDetected && !mDamLatched), evaluated by
    // publishUsbState(). mPubCharging / mPubFault hold the last charging /
    // fault state emitted so a DAM latch can replay a clean disconnect.
    bool mUsbVisible  = false;
    bool mPubCharging = false;
    bool mPubFault    = false;

    // Set once a fault-free USB Protection state is observed within the
    // current cable session. A fault that appears only after a healthy
    // period is treated as a recoverable glitch (STUSB PD reset kicks the
    // host to re-enumerate); a fault present from the very start of the
    // session is treated as permanent and left for the user to unplug.
    // Reset on disconnect.
    bool mUsbHealthySeen = false;

    bool  mIsUsbDetected = false;
    bool  mIsCharging = false;
    bool  mChargingInhibited = false;
    float mBattLevel = 0;

    static const uint32_t skFuelGaugeServicePeriod    = 5000;
    // Backup power-off after onBatteryLow. Must exceed the full GUI shutdown
    // sequence (10 s shutdown screen + subsystem teardown) so the GUI powers
    // off first; this fires only if it does not.
    static const uint32_t skLowBatteryPowerOffTimeout = 30000;

    // Power-off SOC threshold. The gauge's converge-to-empty routine commonly
    // stalls RepSOC around 0.2% and never reaches exactly 0, so power off below
    // 1% rather than at 0. This is well above VEmpty (3.40 V), so the cell is
    // not deep-discharged.
    static constexpr float skLowBatterySoc = 1.0f;

    // Critical-voltage safety floor for power-off. A defective gauge can report
    // a SOC that stalls well above 1% and never falls, which would let the cell
    // discharge dangerously deep. Below VEmpty but above the PMIC dropout.
    // skMinValidVoltage rejects a 0 reading from an absent gauge.
    static constexpr float skCriticalShutdownVoltage = 3.35f;
    static constexpr float skMinValidVoltage         = 1.00f;

    // RTOS components
    OS::Timer<Power> mFuelGaugeTimer;
    OS::Timer<Power> mLowBattTimer;

    // Array of an observers
    Observer::Holder<StatusCallback, 5> mObserver;

    // PMIC interrupt handler.
    virtual void onIrq() override;

    // USB Fault line
    virtual void onUsbFaultIrq() override;

    // Timer handlers
    void lowBattTimHandler(void *arg);
    void fuelGaugeTimHandler(void *arg);

    void pmicHandler();
    void fuelGaugeHandler();
    void usbProtectionHandler();

    // Reconcile the system-facing USB view (mUsbVisible) with the internal
    // physical/logical state and emit the onUsbDetected/onCharging/onUsbError
    // callbacks. Idempotent: diffs against the last published state.
    void publishUsbState();

    OS::Thread<Power, 4 * 1024> mThread;
    struct Cmd {
        enum class Id : uint8_t {
            NONE = 0,
            IRQ_PWR_LINE,
            IRQ_USB_LINE,
            POWER_OFF,
            REBOOT,
            FUELGAUGE_POLL,
            POWER_LOW_BATTERY_TIMEOUT,
        };
        Id id;

        Cmd() : id(Id::NONE) {}
        Cmd(Id id) : id(id) {}
    };
    OS::Queue<Cmd, 10> mCmdQueue;

    // Coalescing flags. ISR-side handlers atomic-exchange these to "true"
    // and push the corresponding command only when the previous value was
    // "false". The thread-side handler stores "false" right before doing
    // its work, which lets a fresh interrupt arriving during processing
    // enqueue exactly one follow-up command (no duplicate burst in the
    // queue). Prevents queue saturation on USB-C contact bounce / rapid
    // timer fires.
    std::atomic<bool> mPwrLineDirty       { false };
    std::atomic<bool> mUsbLineDirty       { false };
    std::atomic<bool> mFuelGaugePollDirty { false };

    void thread();
    bool sendToQueue(const Cmd& cmd, uint32_t timeout);

    void hIrqPwrLine();
    void hIrqUsbLine();
    void hPowerOff();
    void hReboot();
    void hFuelGaugePoll();
    void hLowBatteryTimeout();
};

#endif /* __POWER_HPP */
