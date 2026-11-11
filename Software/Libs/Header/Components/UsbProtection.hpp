/**
 ******************************************************************************
 * @file    UsbProtection.hpp
 * @brief   USB Type-C protection and fault monitoring interface.
 ******************************************************************************
 *
 * Provides control and status monitoring for the USB protection path,
 * including STUSB4500 device, VBUS control, DAM line switching,
 * and USB fault interrupt handling.
 *
 ******************************************************************************
 */

#ifndef __USB_PROTECTION_HPP
#define __USB_PROTECTION_HPP

#include "Porting.h"

#include <atomic>

#include "STUSB4500.hpp"
#include "IGpio.hpp"
#include "OS.hpp"

class UsbProtection : public Interface::IExti::Handler
{
public:
    /**
     * @brief   Board variant / operating mode.
     *
     * Unknown  - board not yet classified (USB never connected, or detect in progress).
     * Active   - Rev 3.1: STUSB4500 confirmed, full protection active.
     * Bypassed - Rev 2.1: protection chip absent, all methods are safe no-op.
     *
     * Once Active or Bypassed is reached, the value is sticky until reboot.
     */
    enum class Mode : uint8_t {
        Unknown = 0,
        Active,
        Bypassed,
    };

    static UsbProtection& GetInstance()
    {
        static UsbProtection sInstance;

        return sInstance;
    }

    void config(Hardware::STUSB4500& STUSB4500,
                Interface::IGpo&     damEnablePin,
                Interface::IExti&    usbFaultExti);

    /**
     * @brief   Called by Power when PMIC reports USB cable plugged in.
     *          On the first call with Mode==Unknown performs board detection
     *          (probes STUSB4500 over I2C). On subsequent calls in Active mode
     *          re-arms USB_FAULT_L EXTI. No-op in Bypassed mode.
     */
    void onUsbConnected();

    /**
     * @brief   Called by Power when PMIC reports USB cable unplugged.
     *          Disarms USB_FAULT_L EXTI and clears cached STUSB4500 status.
     *          Mode value is not modified.
     */
    void onUsbDisconnected();

    /**
     * @brief   Current operating mode (see Mode enum).
     */
    Mode getMode();

    /**
     * @brief   Diagnostic helper for ProdTest: verify STUSB4500 presence
     *          (I2C ACK + correct DEVICE_ID). Bypasses Mode state machine.
     *          For production tests only.
     */
    bool isSTUSB4500Present();

    bool isConnected();
    bool isSink();
    bool isDAM();

    bool isVBusFault();
    bool isCCFault();
    bool isDischargeFault();

    struct PowerFaultStatus {
        bool vbusFault  = false;
        bool ccFault    = false;
        bool dischFault = false;

        void reset()
        {
            vbusFault  = false;
            ccFault    = false;
            dischFault = false;
        }

        bool any() const
        {
            return vbusFault || ccFault || dischFault;
        }
    };

    bool interruptPowerFaultHandler(PowerFaultStatus& status);

    /**
     * @brief   Soft-reset the STUSB4500 PD state machine.
     *          The CC pull-up drops momentarily during the reset, so the
     *          USB host observes a detach/attach cycle and re-enumerates
     *          the device. Useful to recover host-side USB communication
     *          after a NX20P0407 data-line fault has been cleared.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool resetPdNegotiation();

    /**
     * @brief   Best-effort masking of all STUSB4500 alerts to release the
     *          shared PWR_INT_L line. Used when PMIC reports a disconnect
     *          but STUSB is still powered from the connector (e.g. a
     *          standing CC fault keeps VBUS_EN_SNK Hi-Z, so PMIC sees no
     *          VBUS while STUSB stays alive and holds ALERT). A single
     *          no-retry presence probe gates the write, so a chip that is
     *          actually dead (real unplug) is skipped silently and the
     *          bus is left for waitBusFree() to recover.
     */
    void quenchStusbAlerts();

    /**
     * @brief   Returns 'true' if a NX20P0407 over-voltage fault is
     *          currently asserted on USB_FAULT_L, OR a brief pulse has
     *          been latched in the ISR since the previous call. The
     *          latch is cleared (consumed) on each successful return.
     */
    bool isUsbFault();

    bool enableDAMLines(bool status);
    bool isDAMLinesEnabled();

    bool getStatus(Hardware::STUSB4500::Status& status);

    /**
     * @brief   Callback interface for USB_FAULT_L EXTI events.
     */
    class UsbFaultIrqHandler {
    public:
        /**
         * @brief   Invoked from ISR context on every USB_FAULT_L edge.
         *          Implementations must do only ISR-safe work
         *          (no blocking, no mutex, no I2C).
         */
        virtual void onUsbFaultIrq() = 0;
    };

    /**
     * @brief   Register a handler for USB_FAULT_L EXTI events.
     *          Pass nullptr to detach.
     * @param   handler: callback to invoke, or nullptr.
     */
    void setUsbFaultIrqHandler(UsbFaultIrqHandler* handler);

private:
    UsbProtection();
    ~UsbProtection() = default;

    void extiHandler(Interface::IExti*      exti,
                     Interface::IExti::Edge edge) override;

    bool probeAndConfigure();   // STUSB4500.init() + EXTI arm; called when entering Active
    void armExti();
    void disarmExti();

    void countI2CError();

    static constexpr uint8_t kMaxI2CErrorsCount      = 5;
    static constexpr uint8_t kMaxFailedDetectCycles  = 3;

    bool mIsConfigured;
    bool mIsArmed;

    Interface::IGpo*            mDamEnablePin;
    Hardware::STUSB4500*        mSTUSB4500;
    Hardware::STUSB4500::Status mSTUSB4500Status;
    Interface::IExti*           mUsbFaultExti;

    // ISR-safe slots: extiHandler reads / writes these without locking.
    std::atomic<UsbFaultIrqHandler*> mUsbFaultIrqHandler { nullptr };
    std::atomic<bool>                mFaultLatched       { false };

    Mode                        mMode;
    uint8_t                     mI2CErrorsCount;
    uint8_t                     mFailedDetectCycles;
    OS::Mutex                   mDataMutex;
};

#endif
