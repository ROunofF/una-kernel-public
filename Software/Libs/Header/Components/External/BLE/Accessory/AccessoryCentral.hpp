/**
 ******************************************************************************
 * @file    AccessoryCentral.hpp
 * @brief   Central-role BLE link to an external accessory (e.g. HR strap).
 ******************************************************************************
 *
 * Owns the watch's outbound (central) BLE link: scans for an accessory
 * advertising the profile's service, connects, discovers + subscribes to its
 * characteristic, and streams typed samples. Registers as the central-role
 * Ble::ILinkClient so EventRouter delivers its connection, advertising, GATT
 * and GAP events here.
 *
 * Execution model mirrors ClientANCS: event entry points (ILinkClient) only
 * record state and arm mTimer; ALL ACI calls are issued from timCallback() on
 * the timer thread, where taking the core mutex is safe (never lock
 * across a wait). The acquisition flow runs within bounded budgets.
 *
 * Acquisition connects only to remembered straps (strict allow-list filtering
 * via addAllowedDevice()/isAllowed(), populated from Settings) and runs
 * concurrently with the phone link (proven on hardware). Forcible link-vacation
 * is a later, hardware-gated addition; IBle already exposes the
 * advertising/disconnect hooks and the transfer-quiet gate is honoured.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_ACCESSORY_CENTRAL_HPP
#define __COMPONENT_BLE_ACCESSORY_CENTRAL_HPP

#include <cstdint>

#include "OS.hpp"
#include "CoreBlueNRG.hpp"
#include "EventRouter.hpp"
#include "ILinkClient.hpp"
#include "IBle.hpp"
#include "IAccessoryProfile.hpp"
#include "AccessoryScanner.hpp"
#include "AccessoryLink.hpp"
#include "GapScanOpsBlueNRG.hpp"

namespace Ble
{

/**
 * @brief   Central-role link manager for one external accessory.
 *
 * @note    The GAP discovery scan now lives in AccessoryScanner, which
 *          owns the one global scan resource and matches a SET of service UUIDs.
 *          This class still owns all scheduling policy around it -- burst
 *          timing, the scan-cycle budget, the fast-burst/slow-retry ladder, the
 *          scan-start back-off and the phone-transfer gate -- which moves to the
 *          multi-link supervisor.
 */
class AccessoryCentral : public ILinkClient,
                         public AccessoryScanner::MatchCallback,
                         public AccessoryLink::Observer {
public:

    /**
     * @brief   Sink for decoded accessory samples (wired to the HR source
     *          arbiter).
     */
    class SampleCallback {
    public:
        virtual void onAccessorySample(const AccessorySample &sample) = 0;
    protected:
        virtual ~SampleCallback() = default;
    };

    /**
     * @brief   One accessory found during a discovery scan (settings "Add new").
     */
    struct ScanResult {
        uint8_t  addrType;
        uint8_t  addr[6];
        int8_t   rssi;
        char     name[24];   // from advertising data; empty if not advertised
    };

    /**
     * @brief   Sink for discovery-scan results (the settings screen).
     */
    class ScanCallback {
    public:
        virtual void onScanResult(const ScanResult &result) = 0;
    protected:
        virtual ~ScanCallback() = default;
    };

    /**
     * @brief   Coarse acquisition status for the UI/app status bridge.
     *
     * A pure-BLE view of the link's progress; the Backend maps it onto the
     * SDK::Accessory::State contract it ships to the GUI and (later) apps. Kept
     * here so AccessoryCentral stays free of the SDK message headers.
     */
    enum class LinkStatus : uint8_t {
        Idle,        // not acquiring
        Searching,   // scanning for the accessory (first acquisition)
        Connecting,  // found; connecting / discovering / subscribing
        Connected,   // streaming
        Lost,        // dropped after streaming; re-acquiring
        Failed,      // acquisition gave up (bounded budget exhausted)
    };

    /**
     * @brief   Sink for coarse link-status changes (the Backend, which forwards
     *          to the settings screen and, later, subscribed apps).
     */
    class StatusCallback {
    public:
        virtual void onAccessoryStatus(LinkStatus status, const char *name) = 0;
    protected:
        virtual ~StatusCallback() = default;
    };

    /**
     * @brief   Acquisition outcome reported when the link drops to FAILED.
     */
    enum class FailReason : uint8_t {
        None,
        NotFound,         // scan budget exhausted, no matching accessory
        CouldNotConnect,  // connect attempts exhausted
        DiscoveryFailed,  // characteristic discovery/subscribe failed
        Wedged,           // controller signalling sustained overload
    };

    /**
     * @brief   Construct and register as the central-role link client.
     * @param   core: BlueNRG core (ACI access + mutex).
     * @param   profile: accessory profile (UUIDs, discovery, sample decode).
     * @param   phone: phone (peripheral) link, for the transfer-quiet coexistence
     *           gate and the future link-vacation hooks.
     */
    AccessoryCentral(CoreBlueNRG &core, IAccessoryProfile &profile,
            Interface::IBle &phone);

    ~AccessoryCentral() override;

    /**
     * @brief   The single constructed instance (one accessory link in v1), or
     *          nullptr if BLE is absent. Lets the Backend-side AccessoryManager
     *          reach it without threading a reference through construction.
     */
    static AccessoryCentral* instance() { return spInstance; }

    /**
     * @brief   Begin acquiring the accessory (scan -> connect -> stream).
     *          No-op if already acquiring/streaming. Call from the workout
     *          prepare path.
     */
    void requestAcquire();

    /**
     * @brief   Tear down: disconnect the accessory, stop scanning, return to
     *          IDLE. Safe to call from any state.
     */
    void release();

    /**
     * @brief   Whether accessory samples are currently streaming.
     */
    bool isStreaming() const { return mLink.isStreaming(); }

    /**
     * @brief   Attach (or clear, nullptr) the decoded-sample sink.
     */
    void attachSampleCallback(SampleCallback *cb) { mpSampleCb = cb; }

    /**
     * @brief   Attach (or clear, nullptr) the link-status sink. The current
     *          status is pushed on the next transition; call attachAndEmit-style
     *          callers can read isStreaming() for the initial value.
     */
    void attachStatusCallback(StatusCallback *cb) { mpStatusCb = cb; }

    /**
     * @brief   Allow-list of remembered-strap addresses acquisition may connect
     *          to (strict filtering): acquisition ignores any advertiser
     *          not on this list. Call clearAllowedDevices() then addAllowedDevice()
     *          per saved strap before requestAcquire(). An empty list matches
     *          nothing — a mis-call never grabs a stranger's strap. Discovery
     *          scans (settings "Add new") are unaffected and report all straps.
     */
    void clearAllowedDevices() { mAllowedCount = 0; }
    void addAllowedDevice(const uint8_t addr[6]);

    /**
     * @brief   Start a discovery scan for the settings screen: report every
     *          matching accessory (no connect) until stopDiscoveryScan().
     *          No-op if an acquisition is already in progress.
     * @param   cb: sink for results (one call per newly-seen device).
     * @retval  'true' if discovery started.
     */
    bool startDiscoveryScan(ScanCallback *cb);

    /**
     * @brief   Stop the discovery scan and return to idle.
     */
    void stopDiscoveryScan();

    // --- Ble::ILinkClient (central link, via EventRouter) ------------------
    void onConnected(uint16_t connHandle, uint8_t peerAddrType,
            const uint8_t peerAddr[6], uint16_t interval, uint16_t latency,
            uint16_t supTimeout) override;
    void onDisconnected(uint16_t connHandle, uint8_t reason) override;
    void onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
            uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
            uint16_t supTimeout) override;
    void onConnUpdateComplete(uint16_t connHandle, uint8_t status,
            uint16_t interval, uint16_t latency, uint16_t supTimeout) override;

private:
    /**
     * @brief   Stop the scan with the core mutex held.
     * @note    The scanner's held-match table is mutated from the HCI event
     *          thread, the timer task and the app thread; the core mutex is
     *          what serialises them. See the definition for the corruption this
     *          prevents.
     */
    void stopScanLocked();

public:
    void onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *value, uint8_t len) override;
    void onNotification(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *data, uint8_t len) override;
    void onGattProcComplete(uint16_t connHandle, uint8_t errorCode) override;
    void onAdvReport(uint8_t addrType, const uint8_t addr[6], uint8_t dataLen,
            const uint8_t *data, int8_t rssi, uint8_t eventType) override;
    void onGapProcComplete(uint8_t procedureCode, uint8_t status,
            const uint8_t *data, uint8_t len) override;

    // --- Ble::AccessoryScanner::MatchCallback -----------------------------
    /**
     * @brief   An advertiser matched a wanted service UUID during a scan burst.
     *          Routes to the settings discovery list, or (for acquisition)
     *          applies the remembered-device allow-list and starts the connect.
     */
    void onScanMatch(const AccessoryScanner::Match &match) override;

    // --- Ble::AccessoryLink::Observer -------------------------------------
    void onLinkSample(AccessoryLink &link,
            const AccessorySample &sample) override;
    void onLinkStatus(AccessoryLink &link, AccessoryLink::Status status) override;
    void onLinkClosed(AccessoryLink &link, AccessoryLink::CloseReason reason,
            bool everStreamed) override;

private:

    // SUPERVISOR states only: find a peer and hand it to a link. Everything from
    // connect onwards is AccessoryLink's own state machine.
    // SCANNING <-> SCAN_COOLDOWN -> TERMINATE_PENDING -> CONNECT_WAIT -> [link];
    // FAILED; re-acquire via RESCAN_PENDING.
    enum class State : uint8_t {
        IDLE,
        SCANNING,
        SCAN_COOLDOWN,      // between bursts / phone-busy pause / backoff
        RESCAN_PENDING,
        TERMINATE_PENDING,  // accessory found; terminate scan before connect
        CONNECT_WAIT,       // await scan-terminate proc-complete, then hand over
        LINKED,             // a link owns the peer; supervisor is not scanning
        FAILED,
    };

    static AccessoryCentral *spInstance;

    CoreBlueNRG      &mCore;
    IAccessoryProfile &mProfile;
    Interface::IBle  &mPhone;

    // Declared in this order deliberately: mScanner holds a reference to
    // mScanOps, so mScanOps must be constructed first.
    GapScanOpsBlueNRG mScanOps;
    AccessoryScanner  mScanner;
    // One link today.
    AccessoryLink     mLink;
    SampleCallback   *mpSampleCb { nullptr };
    StatusCallback   *mpStatusCb { nullptr };

    State    mState { State::IDLE };

    // Peer found by the scan, held only until the hand-over to mLink completes.
    uint8_t  mPendingAddr[6] { };
    uint8_t  mPendingAddrType { 0 };
    char     mPendingName[24] { };

    // Allow-list of remembered-strap addresses acquisition may connect to
    // (strict filtering). Populated from Settings::Accessories by the
    // AccessoryManager before requestAcquire(); empty => connect to nothing.
    static constexpr uint8_t kMaxAllowed = 8;  // = WatchSettingsAccessories::kMaxAccessories
    uint8_t  mAllowed[kMaxAllowed][6] {};
    uint8_t  mAllowedCount { 0 };
    bool isAllowed(const uint8_t addr[6]) const;

    // Status-bridge bookkeeping: last coarse status reported (de-dup), and
    // whether this acquisition session ever reached STREAMING (distinguishes a
    // first search from a re-acquire after a drop -> Searching vs Lost).
    LinkStatus mLastStatus { LinkStatus::Idle };
    bool       mEverStreamed { false };

    uint16_t mScanCycles { 0 };       // bounded-scan budget
    uint8_t  mScanFailStreak { 0 };   // consecutive scan-start failures
    bool     mScanPausedForPhone { false };
    bool     mSlowRetry { false };    // re-acquire ladder: fast burst exhausted, now low-duty

    // Discovery-scan (settings) mode: report devices instead of connecting.
    bool          mDiscovery { false };
    ScanCallback *mpScanCb { nullptr };
    static constexpr uint8_t kSeenMax = 8;
    uint8_t       mSeenAddr[kSeenMax][6] {};
    uint8_t       mSeenCount { 0 };

    OS::Timer<AccessoryCentral> mTimer;
    void timCallback(void *arg);

    // Set mState and, unless in discovery (settings) mode, emit the coarse
    // LinkStatus to the status callback when it changes.
    void setState(State next);
    // Map the current internal state (+ mEverStreamed) to a coarse LinkStatus.
    LinkStatus linkStatus() const;
    // Best available peer name for the status bridge.
    const char *peerNameOrEmpty() const;

    // ACI-issuing steps (timer-thread context; lock the core briefly).
    bool beginScan();
    void terminateDiscovery();
    void fail(FailReason reason);

    /// Hand the pending peer to the link once the scan has stopped.
    void handOverToLink();
    /// Re-arm scanning after a link closed. `resetBudget` restores the fast
    /// burst, which today only a genuine disconnect does.
    void rearmScan(bool resetBudget);
};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_ACCESSORY_CENTRAL_HPP */
