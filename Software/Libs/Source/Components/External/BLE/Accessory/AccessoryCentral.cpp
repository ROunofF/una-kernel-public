/**
 ******************************************************************************
 * @file    AccessoryCentral.cpp
 * @brief   Central-role BLE link to an external accessory (e.g. HR strap).
 ******************************************************************************
 *
 * The acquisition flow runs within bounded budgets. Event entry points only
 * record state + arm the timer; all ACI runs
 * from timCallback() with the core mutex held briefly (never across a wait).
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.Accessory"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "AccessoryCentral.hpp"

#include "AccessoryAdvParser.hpp"

#include <cstring>

#include "una_ble.h"

namespace Ble
{

AccessoryCentral *AccessoryCentral::spInstance = nullptr;

namespace {

// Burst scanning: short scan, idle gap, repeat -- keeps sustained load low so a
// concurrent phone sync is not starved.
constexpr uint32_t kScanBurstMs = 3000;
constexpr uint32_t kScanGapMs   = 5000;

// Delay the FIRST scan after an acquire request. The pre-warm fires at the app's
// onStartGUI (peak GUI-init load); a scan started right then wedged the BlueNRG
// controller -> watchdog. Letting the app settle ~2 s first avoids
// that collision. Subsequent bursts use kScanGapMs as normal.
constexpr uint32_t kAcquireDelayMs = 2000;

// Bounded search: give up after this many burst cycles (resets when found).
constexpr uint16_t kMaxScanCycles = 20;

// Re-acquire ladder (mid-workout dropout). The bounded fast burst above runs for
// kMaxScanCycles (~kScanBurstMs+kScanGapMs each). If it exhausts AFTER a live
// link existed, drop to a low-duty slow periodic retry at this gap for the rest
// of the session instead of giving up -- a dropped strap usually returns. A
// first acquisition that never found the strap still ends in FAILED(NotFound).
// Bounded by the session (release() stops it) and each burst stays watchdog-fed,
// so there is no tight unbounded loop. Forcible link-vacation remains
// deferred.
constexpr uint32_t kSlowRescanGapMs = 30000;  // 30 s between bursts in slow retry

// Suppress scanning / connecting while the phone link is mid-transfer: a
// concurrent GAP procedure starves it (0x64), the scan then fails (0xFF/0x46),
// and sustained contention can wedge the controller.
constexpr uint32_t kPhoneQuietMs   = 2000;
constexpr uint32_t kPhoneRecheckMs = 1000;

// Escalating back-off when scan-start keeps failing (controller overload); stop
// after the cap rather than hammering a strained controller -> FAILED(Wedged).
constexpr uint32_t kScanFailBackoffStepMs   = 2000;
constexpr uint8_t  kScanFailBackoffMaxSteps = 6;

// NOTE: connection parameters, the connect timeout and the discovery-retry cap
// live in AccessoryLink -- they moved with the per-peer FSM. Do not
// reintroduce copies here; a stale duplicate of the relax band outlived the
// move and read 60-200 ms, which was then measured to be a no-op.

} // namespace

AccessoryCentral::AccessoryCentral(CoreBlueNRG &core, IAccessoryProfile &profile,
        Interface::IBle &phone)
    : mCore(core), mProfile(profile), mPhone(phone)
    , mScanOps(core), mScanner(mScanOps)
    , mLink(core, profile, phone, *this)
{
    if (!mTimer.init(LOG_MODULE_PRX, this, &AccessoryCentral::timCallback, false,
            this)) {
        LOG_ERROR("supervisor timer creation failed (RTOS heap?)\n");
    }
    // init() is split out from the link's constructor precisely so this failure
    // is visible: osTimerNew allocates, and osTimerStart on a null handle only
    // returns an error, so a heap-exhausted boot would leave the link silently
    // stuck before connect with nothing in the log to say why.
    if (!mLink.init("BLE.Link")) {
        LOG_ERROR("link timer creation failed (RTOS heap?); "
                  "accessory acquisition will not progress\n");
    }
    spInstance = this;
    // Own the central-role link: EventRouter delivers central connection,
    // advertising, GATT and GAP events here.
    bleEventRouter().setRoleClient(LinkRole::Central, this);
}

AccessoryCentral::~AccessoryCentral()
{
    bleEventRouter().setRoleClient(LinkRole::Central, nullptr);
    if (spInstance == this) {
        spInstance = nullptr;
    }
}

void AccessoryCentral::requestAcquire()
{
    if (mState != State::IDLE && mState != State::FAILED) {
        return;  // already acquiring or streaming
    }
    LOG_INFO("acquire requested\n");
    mScanCycles = 0;
    mScanFailStreak = 0;
    mSlowRetry = false;        // fresh session starts at the fast-burst phase
    mEverStreamed = false;     // fresh session: a search is Searching, not Lost
    mPendingName[0] = '\0';
    // Defer the first scan so it doesn't collide with the app's GUI init (see
    // kAcquireDelayMs). setState(SCAN_COOLDOWN) surfaces "Searching" immediately;
    // the timer then drives the first beginScan() via timCallback().
    setState(State::SCAN_COOLDOWN);
    mTimer.start(kAcquireDelayMs);
}

bool AccessoryCentral::startDiscoveryScan(ScanCallback *cb)
{
    if (mState != State::IDLE && mState != State::FAILED) {
        LOG_WARNING("discovery refused: busy (state %u)\n",
                static_cast<unsigned>(mState));
        return false;  // do not interrupt an acquisition
    }
    LOG_INFO("discovery scan start\n");
    mDiscovery = true;
    mpScanCb = cb;
    mSeenCount = 0;
    mScanFailStreak = 0;
    beginScan();
    return true;
}

void AccessoryCentral::stopDiscoveryScan()
{
    if (!mDiscovery) {
        return;
    }
    LOG_INFO("discovery scan stop\n");
    mTimer.stop();
    stopScanLocked();         // flushes any match still held for a name
    mDiscovery = false;
    mpScanCb = nullptr;
    mState = State::IDLE;
}

void AccessoryCentral::stopScanLocked()
{
    // The scanner's held-match table is touched from THREE threads: the HCI
    // event thread (onAdvReport, already inside CoreBlueNRG's mutex because
    // una_ble_hci_user_evt_proc runs under it), the FreeRTOS timer task (burst end),
    // and the app thread (stopDiscoveryScan / release). Unsynchronised, a
    // flush that zeroes the count while a report is mid swap-remove underflows
    // it to 255 and the next hold writes far past an 8-slot array.
    //
    // Taking the core mutex here puts every mutation on one lock. It is
    // recursive, so this is also safe on the paths that already hold it.
    mCore.lock();
    mScanner.stop();
    mCore.release();
}

void AccessoryCentral::release()
{
    LOG_INFO("release\n");
    mTimer.stop();

    // Cancel whatever radio operation is in flight for the current state -- not
    // just an established link. mTimer is already stopped, so timCallback's own
    // connect/scan-timeout cancels will not fire; release() must do it here or
    // the operation runs until the controller self-times-out (a starved
    // initiator can sit ~60-70 s), holding radio/slot budget and delaying the
    // phone link or the next acquire.
    // The scan cancel goes through the scanner (which also clears its
    // radio-ownership flag); the rest are still direct ACI here.
    switch (mState) {
        case State::SCANNING:
        case State::TERMINATE_PENDING:   // discovery scan still running
        case State::CONNECT_WAIT:        // scan-terminate may not have completed
            stopScanLocked();
            break;
        default:
            break;
    }

    // Settle the supervisor's own state BEFORE closing the link. close() ->
    // finish() -> setState(IDLE) on the link emits onLinkStatus, and
    // linkStatus() would still read mState == LINKED with mEverStreamed set,
    // reporting a spurious Lost immediately before Idle. Pre-extraction
    // release() simply set IDLE, so this ordering keeps that behaviour.
    mEverStreamed = false;
    setState(State::IDLE);

    // The link cancels its own in-flight initiator or terminates its own
    // established connection; the supervisor no longer knows the handle.
    // onLinkClosed() early-returns for CloseReason::Released, so nothing here
    // depends on the close happening first.
    mLink.close();
    // Clear discovery (settings-scan) state too, so a release that races an
    // active discovery scan can't leave a stale callback or discovery mode that
    // a later requestAcquire() would inherit.
    mDiscovery = false;
    mpScanCb   = nullptr;
    mSlowRetry = false;
}

void AccessoryCentral::addAllowedDevice(const uint8_t addr[6])
{
    if (mAllowedCount >= kMaxAllowed) {
        return;
    }
    memcpy(mAllowed[mAllowedCount], addr, 6);
    ++mAllowedCount;
}

bool AccessoryCentral::isAllowed(const uint8_t addr[6]) const
{
    for (uint8_t i = 0; i < mAllowedCount; ++i) {
        if (memcmp(mAllowed[i], addr, 6) == 0) {
            return true;
        }
    }
    return false;
}

void AccessoryCentral::fail(FailReason reason)
{
    LOG_WARNING("acquisition failed (reason %u)\n",
            static_cast<unsigned>(reason));
    mTimer.stop();
    // FAILED before close(), for the same reason as release(): the link's own
    // transition to IDLE emits a status, and a still-LINKED supervisor with
    // mEverStreamed set would report Lost just before Failed.
    setState(State::FAILED);
    mLink.close();
    // Terminal until requestAcquire()/release() -- bounded, never auto-looping.
}

AccessoryCentral::LinkStatus AccessoryCentral::linkStatus() const
{
    switch (mState) {
        case State::IDLE:   return LinkStatus::Idle;
        case State::FAILED: return LinkStatus::Failed;
        case State::SCANNING:
        case State::SCAN_COOLDOWN:
        case State::RESCAN_PENDING:
            // A re-acquire after a drop is "Lost" for the whole search; a first
            // acquisition is "Searching".
            return mEverStreamed ? LinkStatus::Lost : LinkStatus::Searching;
        case State::LINKED:
            // Ask the link: streaming reads Connected, anything earlier is still
            // Connecting (or Lost, if this is a re-acquire).
            if (mLink.isStreaming()) {
                return LinkStatus::Connected;
            }
            return mEverStreamed ? LinkStatus::Lost : LinkStatus::Connecting;
        default:
            // Hand-over window: TERMINATE_PENDING / CONNECT_WAIT.
            return mEverStreamed ? LinkStatus::Lost : LinkStatus::Connecting;
    }
}

const char *AccessoryCentral::peerNameOrEmpty() const
{
    // While a link owns the peer its name is authoritative; during the hand-over
    // window the scanner's pending name is all we have.
    return mLink.isIdle() ? mPendingName : mLink.peerName();
}

void AccessoryCentral::setState(State next)
{
    mState = next;

    // Discovery (settings "Add new") scans drive the same FSM but are a
    // different UX -- a scan-result list, not connection status -- so suppress
    // acquisition-status emission while discovering.
    if (mDiscovery) {
        return;
    }

    LinkStatus status = linkStatus();
    if (status != mLastStatus) {
        mLastStatus = status;
        if (mpStatusCb != nullptr) {
            mpStatusCb->onAccessoryStatus(status, peerNameOrEmpty());
        }
    }
}

bool AccessoryCentral::beginScan()
{
    // Defer (without consuming a scan cycle) while the phone is mid-transfer.
    if (mPhone.isConnected() && !mPhone.isTransferQuiet(kPhoneQuietMs)) {
        if (!mScanPausedForPhone) {
            mScanPausedForPhone = true;
            LOG_INFO("phone syncing - scan paused\n");
        }
        setState(State::SCAN_COOLDOWN);
        mTimer.start(kPhoneRecheckMs);
        return false;
    }
    if (mScanPausedForPhone) {
        mScanPausedForPhone = false;
        LOG_INFO("phone link quiet - resuming scan\n");
    }

    // Discovery (settings) scans indefinitely until stopped; only acquisition's
    // fast burst is bounded. Once in slow retry the cap no longer applies (the
    // session lifetime bounds it instead).
    if (!mDiscovery && !mSlowRetry && ++mScanCycles > kMaxScanCycles) {
        if (mEverStreamed) {
            // Fast re-acquire exhausted, but we had a live link -- keep trying at
            // a low duty (kSlowRescanGapMs) for the rest of the session rather
            // than giving up; a dropped strap usually returns.
            mSlowRetry = true;
            LOG_INFO("fast re-acquire exhausted after %u cycles; slow periodic retry\n",
                    static_cast<unsigned>(kMaxScanCycles));
        } else {
            LOG_INFO("no accessory found after %u scan cycles\n",
                    static_cast<unsigned>(kMaxScanCycles));
            fail(FailReason::NotFound);
            return false;
        }
    }

    // The scanner owns the wanted-UUID set and the ACI call (and logs the
    // failing status itself). Everything below stays here as this class's
    // scheduling policy until it moves to the supervisor.
    mScanner.clearWantedUuids();
    mScanner.addWantedUuid(mProfile.serviceUuid());

    // Set per burst from the mode, not once at the start of a discovery
    // session: release() can tear a settings scan down without going through
    // stopDiscoveryScan(), and a leaked "await names" would then hold the next
    // ACQUISITION's matches -- delaying a connect to the very strap this
    // feature exists for (the H10 advertises no name in its UNA_BLE_ADV_IND).
    mScanner.setAwaitNames(mDiscovery);

    mCore.lock();
    const bool scanStarted = mScanner.start(this);
    mCore.release();

    if (!scanStarted) {
        // 0xFF blocks for the whole HCI timeout holding the core mutex, starving
        // the event pump; once wedged, stop rather than deepen starvation.
        if (mScanFailStreak >= kScanFailBackoffMaxSteps) {
            LOG_ERROR("scan-start wedged after %u failures\n",
                    static_cast<unsigned>(mScanFailStreak));
            fail(FailReason::Wedged);
            return false;
        }
        mScanFailStreak++;
        const uint32_t backoff = kScanGapMs +
                static_cast<uint32_t>(mScanFailStreak) * kScanFailBackoffStepMs;
        LOG_ERROR("scan-start failed; backing off %lu ms\n",
                static_cast<unsigned long>(backoff));
        setState(State::SCAN_COOLDOWN);
        mTimer.start(backoff);
        return false;
    }

    mScanFailStreak = 0;
    setState(State::SCANNING);
    LOG_INFO("scanning for service 0x%04X...\n", mProfile.serviceUuid());
    mTimer.start(kScanBurstMs);  // bound the burst; timCallback cools down
    return true;
}

void AccessoryCentral::timCallback(void *arg)
{
    (void)arg;
    switch (mState) {
        case State::SCANNING:
            // Burst elapsed with no match: stop and idle a gap (set state before
            // terminating so onGapProcComplete ignores this terminate).
            setState(State::SCAN_COOLDOWN);
            stopScanLocked();
            mTimer.start(mSlowRetry ? kSlowRescanGapMs : kScanGapMs);
            break;
        case State::SCAN_COOLDOWN:
        case State::RESCAN_PENDING:
            beginScan();
            break;
        case State::TERMINATE_PENDING:
            terminateDiscovery();
            break;
        case State::CONNECT_WAIT:
            // terminate proc-complete can be dropped under load; hand over
            // anyway (the link's own connect self-heals if not ready).
            LOG_INFO("terminate proc-complete not seen; handing over anyway\n");
            handOverToLink();
            break;
        default:
            // LINKED: the link owns its own timer from here.
            break;
    }
}

void AccessoryCentral::terminateDiscovery()
{
    stopScanLocked();

    // Prefer to connect on the terminate's proc-complete, but that event can be
    // dropped under load -- fall back after a short wait.
    setState(State::CONNECT_WAIT);
    mTimer.start(500);
}

// --- Ble::ILinkClient (central link) ---------------------------------------

void AccessoryCentral::onAdvReport(uint8_t addrType, const uint8_t addr[6],
        uint8_t dataLen, const uint8_t *data, int8_t rssi, uint8_t eventType)
{
    if (mState != State::SCANNING) {
        return;
    }
    // The scanner filters the report against the wanted-UUID set and calls back
    // into onScanMatch() below; the decision logic there is unchanged. The
    // event type lets it recognise a scan response, which is how a peer whose
    // Local Name is only in that response (the Polar H10) gets a name.
    mScanner.onAdvReport(addrType, addr, data, dataLen, rssi, eventType);
}

void AccessoryCentral::onScanMatch(const AccessoryScanner::Match &match)
{
    const uint8_t *addr     = match.addr;
    const uint8_t  addrType = match.addrType;
    const int8_t   rssi     = match.rssi;

    if (mDiscovery) {
        // Settings scan: report each newly-seen device, never connect.
        for (uint8_t i = 0; i < mSeenCount; ++i) {
            if (memcmp(mSeenAddr[i], addr, 6) == 0) {
                return;  // already reported
            }
        }
        if (mSeenCount < kSeenMax) {
            memcpy(mSeenAddr[mSeenCount], addr, 6);
            ++mSeenCount;
        }
        if (mpScanCb != nullptr) {
            ScanResult r {};
            r.addrType = addrType;
            memcpy(r.addr, addr, 6);
            r.rssi = rssi;
            memcpy(r.name, match.name, sizeof(r.name));
            mpScanCb->onScanResult(r);
        }
        return;
    }

    // Strict filtering: only connect to a remembered strap. Anything
    // else advertising HR (a stranger's strap in a gym/group) is ignored so we
    // keep scanning for ours.
    if (!isAllowed(addr)) {
        return;
    }

    LOG_INFO("found accessory %02X:%02X:%02X:%02X:%02X:%02X rssi %d\n",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], rssi);

    // Held only until the hand-over; the link owns the peer from then on.
    memcpy(mPendingAddr, addr, sizeof(mPendingAddr));
    mPendingAddrType = addrType;
    memcpy(mPendingName, match.name, sizeof(mPendingName));
    mScanCycles = 0;  // found one -- reset the search budget

    // One GAP procedure at a time: terminate the scan, then hand over (timer).
    setState(State::TERMINATE_PENDING);
    mTimer.start(50);
}

void AccessoryCentral::handOverToLink()
{
    // The scan is stopped by now, so the radio is free for the initiator.
    setState(State::LINKED);
    mLink.begin(mPendingAddr, mPendingAddrType, mPendingName);
}

void AccessoryCentral::rearmScan(bool resetBudget)
{
    if (resetBudget) {
        // A live link came back down: restart the fast-burst ladder, since a
        // dropped accessory usually returns.
        mScanCycles = 0;
        mSlowRetry  = false;
    }
    setState(resetBudget ? State::RESCAN_PENDING : State::SCAN_COOLDOWN);
    mTimer.start(mSlowRetry ? kSlowRescanGapMs : kScanGapMs);
}

void AccessoryCentral::onConnected(uint16_t connHandle, uint8_t peerAddrType,
        const uint8_t peerAddr[6], uint16_t interval, uint16_t latency,
        uint16_t supTimeout)
{
    (void)peerAddrType; (void)peerAddr; (void)latency; (void)supTimeout;
    // Routed to the link that initiated it; a master-role connection we did not
    // initiate is simply not claimed by any link. The negotiated interval goes
    // with it so the link knows what it is running BEFORE any relax -- without
    // that, a relax failing at the link layer can only report "keeps interval 0".
    mLink.onConnected(connHandle, interval);
}

void AccessoryCentral::onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *value, uint8_t len)
{
    mLink.onCharByUuidResp(connHandle, attrHandle, value, len);
}

void AccessoryCentral::onGattProcComplete(uint16_t connHandle, uint8_t errorCode)
{
    mLink.onGattProcComplete(connHandle, errorCode);
}

void AccessoryCentral::onNotification(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *data, uint8_t len)
{
    mLink.onNotification(connHandle, attrHandle, data, len);
}

void AccessoryCentral::onDisconnected(uint16_t connHandle, uint8_t reason)
{
    mLink.onDisconnected(connHandle, reason);
}

void AccessoryCentral::onConnUpdateComplete(uint16_t connHandle, uint8_t status,
        uint16_t interval, uint16_t latency, uint16_t supTimeout)
{
    mLink.onConnUpdateComplete(connHandle, status, interval, latency,
            supTimeout);
}

void AccessoryCentral::onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
        uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
        uint16_t supTimeout)
{
    mLink.onL2capUpdateReq(connHandle, identifier, intervalMin, intervalMax,
            latency, supTimeout);
}

// --- Ble::AccessoryLink::Observer ------------------------------------------

void AccessoryCentral::onLinkSample(AccessoryLink &link,
        const AccessorySample &sample)
{
    (void)link;
    // `value` is the HR field and stays 0 for the cycling kinds, whose payload
    // lives in the flag-gated cycling block -- log the kind so a CPS/CSC sample
    // is not mistaken for a broken HR one.
    LOG_DEBUG("sample kind %u value %u trust %u\n",
            static_cast<unsigned>(sample.kind), sample.value, sample.trust);
    if (mpSampleCb != nullptr) {
        mpSampleCb->onAccessorySample(sample);
    }
}

void AccessoryCentral::onLinkStatus(AccessoryLink &link,
        AccessoryLink::Status status)
{
    (void)link;
    if (status == AccessoryLink::Status::Streaming) {
        // Session-level: distinguishes a first search from a re-acquire after a
        // drop (Searching vs Lost), and gates the slow-retry ladder.
        mEverStreamed = true;
    }
    // The supervisor's own setState() emits the coarse LinkStatus; re-emit here
    // so a link-driven transition (connected/streaming) reaches the UI too.
    const LinkStatus next = linkStatus();
    if (next != mLastStatus) {
        mLastStatus = next;
        if (!mDiscovery && mpStatusCb != nullptr) {
            mpStatusCb->onAccessoryStatus(next, peerNameOrEmpty());
        }
    }
}

void AccessoryCentral::onLinkClosed(AccessoryLink &link,
        AccessoryLink::CloseReason reason, bool everStreamed)
{
    (void)link; (void)everStreamed;
    if (reason == AccessoryLink::CloseReason::Released) {
        return;  // deliberate teardown: release() drives the state itself
    }

    // Reproduces the pre-extraction behaviour exactly. A genuine disconnect
    // restarts the fast-burst ladder ("it will probably come back"); a failed
    // connect attempt does not, so a peer we can never reach cannot hold the
    // budget open forever.
    const bool disconnected =
            (reason == AccessoryLink::CloseReason::Disconnected);
    LOG_INFO("link closed (reason %u); rescanning\n",
            static_cast<unsigned>(reason));
    rearmScan(/*resetBudget=*/disconnected);
}

void AccessoryCentral::onGapProcComplete(uint8_t procedureCode, uint8_t status,
        const uint8_t *data, uint8_t len)
{
    (void)status; (void)data; (void)len;
    if (procedureCode != UNA_BLE_GAP_GENERAL_DISCOVERY_PROC) {
        return;
    }
    // The controller has ended the discovery procedure, so the scanner no longer
    // holds the radio. Tell it, or isScanning() would stay latched and block the
    // next start() -- including the connect that this event usually precedes.
    mScanner.onDiscoveryComplete();

    if (mState == State::CONNECT_WAIT) {
        // The scan-terminate (because we found an accessory) completed -- the
        // radio is free, so hand the peer to the link.
        mTimer.stop();
        handOverToLink();
    } else if (mState == State::SCANNING) {
        // Bounded discovery window ended without a match -- re-arm scanning,
        // honouring the slow-retry duty cycle so this path can't collapse the
        // 30 s low-duty back-off into continuous scanning.
        setState(State::RESCAN_PENDING);
        mTimer.start(mSlowRetry ? kSlowRescanGapMs : 50);
    }
}

}  /* namespace Ble */
