/**
 ******************************************************************************
 * @file    AccessoryLink.cpp
 * @brief   One central-role connection to an accessory, from connect to stream.
 ******************************************************************************
 *
 * Moved wholesale from AccessoryCentral. The ACI calls, their
 * parameters, the retry counts and every timer interval are unchanged: this is
 * an extraction, not a redesign. What changed is ownership -- the link no longer
 * reaches into scan policy on failure, it reports closure and lets the
 * supervisor decide.
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.Link"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "AccessoryLink.hpp"

#include <cstring>

#include "ConnParamNegotiation.hpp"

#include "una_ble.h"

namespace Ble
{

namespace {

// GAP is initialised with controller privacy, so only a resolvable private
// own-address is accepted; public/random are rejected 0x42.
constexpr uint8_t kOwnAddrType = UNA_BLE_RESOLVABLE_PRIVATE_ADDR;

// Initiator scan parameters -- the window the controller uses while trying to
// catch the peer's advertisement. Same values as the discovery scan.
constexpr uint16_t kInitScanInterval = 0x00A0;
constexpr uint16_t kInitScanWindow   = 0x0040;  // 40 ms

// Connect-time params: a WIDE window (30-200 ms) so the slot scheduler can land
// on a harmonic of the phone's anchor period; narrow windows are rejected 0x84.
// Relaxed once the link is up to free radio for the phone.
constexpr uint16_t kConnIntervalMin = 0x0018;  // 30 ms
constexpr uint16_t kConnIntervalMax = 0x00A0;  // 200 ms
constexpr uint16_t kConnLatency     = 0x0000;
constexpr uint16_t kSupervisionTo   = 0x00C8;  // 2000 ms
constexpr uint16_t kCeLenMin        = 0x0000;
constexpr uint16_t kCeLenMax        = 0x0000;

// Relax to a SLOW window once streaming. Every accessory profile we carry
// (HRS, CPS, CSC) notifies at 1-4 Hz, so a fast interval buys nothing and costs
// the peer's battery -- and some peers enforce that. Measured on HW: the Polar
// H10 asks for 311-500 ms latency 1 and, if left at ~115 ms, streams for
// exactly 120 s and then goes silent while holding the link up.
//
// The window must EXCLUDE the typical connect-time interval or it is a no-op:
// the previous 60-200 ms window completed status 0x00 and changed nothing,
// because the live 115 ms already satisfied it. Only the supervision timeout
// moved. Requesting 311-500 ms moves the interval (measured: settles at 405 ms)
// and the H10 then streams indefinitely without ever sending a request.
constexpr uint16_t kRelaxIntervalMin = 0x00F9;  // 249 units = 311.25 ms
constexpr uint16_t kRelaxIntervalMax = 0x0190;  // 400 units = 500 ms
constexpr uint16_t kRelaxLatency     = 0x0001;
constexpr uint16_t kRelaxSupervision = 0x0258;  // 6000 ms

// Ceiling for a supervision timeout widened to satisfy the Core-spec relation.
constexpr uint16_t kPeerSupervisionMax   = 3200;    // 32000 ms

// Suppress connecting while the phone link is mid-transfer: a concurrent GAP
// procedure starves it (0x64) and sustained contention can wedge the
// controller.
constexpr uint32_t kPhoneQuietMs   = 2000;
constexpr uint32_t kPhoneRecheckMs = 1000;

// Bound the connect attempt: a starved initiator can sit 60-70 s then drop
// 0x3E. Cancel and report closure after this.
constexpr uint32_t kConnectTimeoutMs = 15000;

// GATT discovery can transiently time out (0xFF) under load; retry before
// dropping the half-open link.
constexpr uint8_t kMaxDiscoverRetries = 5;

// Bound the wait for a discovery response. DISCOVERING and DISCOVERED armed no
// timer, so a peer that accepted the connection and then never answered -- or a
// dropped proc-complete, which CONNECT_WAIT's own 500 ms fallback exists because
// the code already assumes can happen -- wedged the link with an open
// connection and the supervisor parked in LINKED, also with no timer. Recovery
// then needed release() or the peer giving up, and the cost was the workout's
// HR for the rest of the session.
//
// Measured discovery is ~1.3 s (connect to proc-complete) on the four straps,
// so 10 s is far outside normal while still acting before the stack's own 30 s
// GATT procedure timeout.
constexpr uint32_t kDiscoverRespTimeoutMs = 10000;

}  // namespace

AccessoryLink::AccessoryLink(CoreBlueNRG &core, IAccessoryProfile &profile,
        Interface::IBle &phone, Observer &observer)
    : mCore(core), mProfile(profile), mPhone(phone), mObserver(observer)
{
}

bool AccessoryLink::init(const char *name)
{
    // osTimerNew allocates from the RTOS heap. A link with a dead timer would
    // silently never progress past connect, so surface the failure rather than
    // discovering it as "this accessory just doesn't work".
    if (!mTimer.init(name, this, &AccessoryLink::timCallback, false, this)) {
        LOG_ERROR("link timer creation failed (RTOS heap?)\n");
        return false;
    }
    return true;
}

void AccessoryLink::setState(State next)
{
    mState = next;

    Status status = Status::Idle;
    switch (next) {
        case State::IDLE:
            status = Status::Idle;
            break;
        case State::STREAMING:
            status = Status::Streaming;
            mEverStreamed = true;
            break;
        default:
            status = Status::Connecting;
            break;
    }

    if (status != mLastStatus) {
        mLastStatus = status;
        mObserver.onLinkStatus(*this, status);
    }
}

void AccessoryLink::begin(const uint8_t addr[6], uint8_t addrType,
        const char *name)
{
    memcpy(mPeerAddr, addr, sizeof(mPeerAddr));
    mPeerAddrType = addrType;
    if (name != nullptr) {
        strncpy(mPeerName, name, sizeof(mPeerName) - 1);
        mPeerName[sizeof(mPeerName) - 1] = '\0';
    } else {
        mPeerName[0] = '\0';
    }
    mConnHandle = 0;
    mDiscoverRetries = 0;
    mEverStreamed = false;
    mPendingL2cap = { };
    mActualInterval = 0;
    mDropReason = CloseReason::Disconnected;
    mProfile.reset();

    setState(State::CONNECT_PENDING);
    mTimer.start(50);
}

void AccessoryLink::close()
{
    mTimer.stop();
    if (mState == State::IDLE) {
        return;
    }

    mCore.lock();
    if (mState == State::CONNECTING) {
        // Initiator still in flight and mConnHandle not yet set.
        una_ble_gap_terminate_gap_proc(UNA_BLE_GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);
    } else if (mConnHandle != 0) {
        una_ble_gap_terminate(mConnHandle, 0x13 /* remote user terminated */);
    }
    mCore.release();

    finish(CloseReason::Released);
}

void AccessoryLink::finish(CloseReason reason)
{
    mTimer.stop();
    const bool everStreamed = mEverStreamed;
    mConnHandle = 0;
    mDiscoverRetries = 0;
    mProfile.reset();
    // Back to the default, or the NEXT link's genuine peer drop would inherit
    // this one's DiscoveryFailed and be denied its scan-budget reset.
    mDropReason = CloseReason::Disconnected;
    setState(State::IDLE);
    mEverStreamed = false;
    mObserver.onLinkClosed(*this, reason, everStreamed);
}

bool AccessoryLink::dropLink(CloseReason reason)
{
    // Record why we are dropping BEFORE the terminate. The disconnection it
    // provokes reaches onDisconnected() indistinguishable from the peer going
    // away on its own, and reporting Disconnected there tells the supervisor to
    // RESET the scan budget -- so a peer that reliably connects and then never
    // answers discovery would loop connect -> timeout -> drop -> fast rescan
    // forever, never escalating to the slow ladder and never reaching
    // NotFound. DiscoveryFailed lets the existing budget bound it.
    mDropReason = reason;

    // Terminate a half-open link; onDisconnected() completes the teardown.
    mCore.lock();
    const una_ble_status status =
            una_ble_gap_terminate(mConnHandle, 0x13 /* remote user terminated */);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        // The caller closes its own state so the link does not wedge, but the
        // controller connection stays up until the peer drops it or the
        // controller is reset: no disconnection event will match it once
        // mConnHandle is cleared. Logged with the handle so the slot can be
        // traced.
        LOG_ERROR("terminate refused [0x%02X] on handle 0x%04X; the controller "
                  "connection stays open\n", status, mConnHandle);
        return false;
    }
    return true;
}

void AccessoryLink::timCallback(void *arg)
{
    (void)arg;
    switch (mState) {
        case State::CONNECT_PENDING:
            createConnection();
            break;
        case State::CONNECTING:
            // create_connection didn't complete in time; cancel the initiator
            // and hand the slot back.
            LOG_ERROR("connect timed out; cancelling initiator\n");
            mCore.lock();
            una_ble_gap_terminate_gap_proc(
                    UNA_BLE_GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);
            mCore.release();
            finish(CloseReason::ConnectFailed);
            break;
        case State::DISCOVER_PENDING:
            discoverCharacteristic();
            break;
        case State::SUBSCRIBE_PENDING:
            enableNotifications();
            break;
        case State::TUNING_PENDING:
            relaxConnInterval();
            break;
        case State::DISCOVERING:
        case State::DISCOVERED:
            // The peer took the connection and then went quiet. Drop rather
            // than reissue: from the controller's side the discovery procedure
            // is still in flight, so a retry would collide with it (0x46) and
            // stack a second failure on the first. Dropping hands the peer back
            // to the supervisor's rescan ladder -- the same recovery a 0x3E
            // takes, which is proven on hardware.
            LOG_ERROR("no discovery response in %lu ms (state %u); "
                      "dropping link\n",
                    static_cast<unsigned long>(kDiscoverRespTimeoutMs),
                    static_cast<unsigned>(mState));
            // This path exists to GUARANTEE an escape, so it cannot depend on
            // the terminate being accepted: if the controller refuses it -- the
            // very contention that tends to make a peer go quiet -- no
            // disconnection event follows, the one-shot timer is spent, and the
            // link would wedge exactly as before, now with a log line claiming
            // it acted. Close it ourselves and hand the slot back.
            if (!dropLink(CloseReason::DiscoveryFailed)) {
                finish(CloseReason::DiscoveryFailed);
            }
            break;
        default:
            break;
    }
}

void AccessoryLink::createConnection()
{
    // Gate on the phone link being quiet (mirrors the scan gate); a long sync
    // must not block forever -- best-effort once the phone has been quiet.
    if (mPhone.isConnected() && !mPhone.isTransferQuiet(kPhoneQuietMs)) {
        setState(State::CONNECT_PENDING);
        mTimer.start(kPhoneRecheckMs);
        return;
    }

    mCore.lock();
    una_ble_status status = una_ble_gap_create_connection(
            kInitScanInterval, kInitScanWindow,
            mPeerAddrType, mPeerAddr,
            kOwnAddrType,
            kConnIntervalMin, kConnIntervalMax,
            kConnLatency, kSupervisionTo,
            kCeLenMin, kCeLenMax);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_ERROR("create_connection failed [0x%02X]\n", status);
        finish(CloseReason::ConnectFailed);
        return;
    }
    setState(State::CONNECTING);
    LOG_INFO("connecting to accessory...\n");
    mTimer.start(kConnectTimeoutMs);  // self-heal if completion never arrives
}

void AccessoryLink::discoverCharacteristic()
{
    una_ble_uuid uuid { };
    uuid.uuid_16 = mProfile.characteristicUuid();

    mCore.lock();
    una_ble_status status = una_ble_gatt_disc_char_by_uuid(
            mConnHandle, 0x0001, 0xFFFF, /*UUID_Type 16-bit=*/0x01, &uuid);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        if (++mDiscoverRetries <= kMaxDiscoverRetries) {
            LOG_ERROR("disc_char_by_uuid failed [0x%02X]; retry %u/%u\n",
                    status, static_cast<unsigned>(mDiscoverRetries),
                    static_cast<unsigned>(kMaxDiscoverRetries));
            setState(State::DISCOVER_PENDING);
            mTimer.start(500);
        } else {
            LOG_ERROR("disc_char_by_uuid failed [0x%02X]; dropping link\n",
                    status);
            // A refused terminate produces no disconnection event, so without
            // this the link sits in DISCOVER_PENDING with no timer armed and
            // the supervisor parks in LINKED -- the same wedge the discovery
            // timeout exists to remove, reopened on a different path.
            if (!dropLink(CloseReason::DiscoveryFailed)) {
                finish(CloseReason::DiscoveryFailed);
            }
        }
        return;
    }
    setState(State::DISCOVERING);
    // Response -> una_ble_gatt_disc_read_char_by_uuid_resp_event -> onCharByUuidResp.
    mTimer.start(kDiscoverRespTimeoutMs);
}

void AccessoryLink::enableNotifications()
{
    uint8_t cccd[2] = { 0x01, 0x00 };  // notifications enabled

    mCore.lock();
    una_ble_status status = una_ble_gatt_write_char_desc(
            mConnHandle, mProfile.cccdHandle(), sizeof(cccd), cccd);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_ERROR("CCCD write failed [0x%02X]; dropping link\n", status);
        // As above: a refused terminate would strand this in SUBSCRIBE_PENDING
        // with no timer.
        if (!dropLink(CloseReason::DiscoveryFailed)) {
            finish(CloseReason::DiscoveryFailed);
        }
        return;
    }
    LOG_INFO("subscribed; relaxing connection interval\n");
    setState(State::TUNING_PENDING);
    mTimer.start(500);
}

void AccessoryLink::relaxConnInterval()
{
    mCore.lock();
    una_ble_status status = una_ble_gap_start_connection_update(
            mConnHandle, kRelaxIntervalMin, kRelaxIntervalMax,
            kRelaxLatency, kRelaxSupervision, kCeLenMin, kCeLenMax);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_WARNING("conn interval relax failed [0x%02X] (staying tight)\n",
                status);
    }
    setState(State::STREAMING);
    LOG_INFO("streaming\n");
}

// --- events routed here by the supervisor ----------------------------------

bool AccessoryLink::onConnected(uint16_t connHandle, uint16_t interval)
{
    if (mState != State::CONNECTING) {
        return false;  // not ours: a link we didn't initiate, or wrong phase
    }
    mConnHandle = connHandle;
    mActualInterval = interval;   // what we run until a relax changes it
    mDiscoverRetries = 0;
    LOG_INFO("accessory connected, handle 0x%04X; discovering 0x%04X\n",
            connHandle, mProfile.characteristicUuid());
    setState(State::DISCOVER_PENDING);
    mTimer.start(100);
    return true;
}

bool AccessoryLink::onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *value, uint8_t len)
{
    if (connHandle != mConnHandle || mConnHandle == 0 ||
            mState != State::DISCOVERING) {
        return false;
    }
    if (mProfile.onCharByUuidResp(attrHandle, value, len)) {
        LOG_INFO("discovered char: value 0x%04X, CCCD 0x%04X\n",
                mProfile.valueHandle(), mProfile.cccdHandle());
        // Wait for the discovery procedure's proc-complete before the CCCD
        // write (only one GATT procedure at a time, else 0x46).
        setState(State::DISCOVERED);
        mTimer.start(kDiscoverRespTimeoutMs);   // proc-complete can go missing
    }
    return true;
}

bool AccessoryLink::onGattProcComplete(uint16_t connHandle, uint8_t errorCode)
{
    if (connHandle != mConnHandle || mState != State::DISCOVERED) {
        return false;  // the later CCCD write also completes here, state differs
    }
    LOG_INFO("discovery complete (0x%02X); enabling notifications\n", errorCode);
    setState(State::SUBSCRIBE_PENDING);
    mTimer.start(50);
    return true;
}

bool AccessoryLink::onNotification(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *data, uint8_t len)
{
    if (connHandle != mConnHandle || mConnHandle == 0) {
        return false;
    }
    AccessorySample sample;
    if (!mProfile.parseNotification(attrHandle, data, len, sample)) {
        return true;  // ours, but not a usable sample
    }
    mObserver.onLinkSample(*this, sample);
    return true;
}

bool AccessoryLink::onDisconnected(uint16_t connHandle, uint8_t reason)
{
    if (connHandle != mConnHandle || mConnHandle == 0) {
        return false;
    }
    LOG_INFO("accessory disconnected (handle 0x%04X) reason 0x%02X\n",
            connHandle, reason);
    // mDropReason stays Disconnected unless dropLink() set it, so a genuine
    // peer drop still resets the scan budget -- "it will probably come back".
    finish(mDropReason);
    return true;
}

bool AccessoryLink::onConnUpdateComplete(uint16_t connHandle, uint8_t status,
        uint16_t interval, uint16_t latency, uint16_t supTimeout)
{
    if (connHandle != mConnHandle || mConnHandle == 0) {
        return false;
    }
    (void)supTimeout;

    if (status != UNA_BLE_STATUS_SUCCESS) {
        // The command was accepted and the PROCEDURE still failed. 0x84/0x85
        // are the anchor-slot rejections. The link stays at whatever it had --
        // typically the connect-time interval -- so say so loudly: this is the
        // state in which a peer that cares about its parameters goes quiet with
        // nothing else in the log to explain it.
        LOG_WARNING("conn update FAILED [0x%02X]; link keeps interval %u "
                    "(%u ms)\n",
                status, (unsigned)mActualInterval,
                (unsigned)((mActualInterval * 5u) / 4u));
        return true;
    }

    mActualInterval = interval;

    // Verify rather than assume the relax landed inside the band we answer
    // peers with. Outside it, we are running something we would not have
    // promised, and any peer told "accepted" was told something untrue.
    if (mState == State::STREAMING
            && (interval < kRelaxIntervalMin || interval > kRelaxIntervalMax)) {
        LOG_WARNING("interval %u (%u ms) is OUTSIDE the relax band %u-%u; "
                    "a peer expecting its request may stop streaming\n",
                (unsigned)interval, (unsigned)((interval * 5u) / 4u),
                (unsigned)kRelaxIntervalMin, (unsigned)kRelaxIntervalMax);
        return true;
    }

    LOG_INFO("link params confirmed: interval %u (%u ms) latency %u\n",
            (unsigned)interval, (unsigned)((interval * 5u) / 4u),
            (unsigned)latency);
    return true;
}

bool AccessoryLink::onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
        uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
        uint16_t supTimeout)
{
    if (connHandle != mConnHandle || mConnHandle == 0) {
        return false;
    }

    // Event context: record it and answer from the BLE work queue. Overwriting
    // an unsent request is correct -- the peer retransmits with a new
    // identifier, and answering the stale one would be answering a question it
    // has stopped asking.
    mPendingL2cap = { connHandle, intervalMin, intervalMax, latency,
                      supTimeout, identifier, true };

    if (!mCore.post(&AccessoryLink::l2capWorkTrampoline, this)) {
        // Queue full. Dropping the response is not fatal to the link -- the
        // peer retransmits -- but it is why a peer may fall silent, so say so.
        LOG_WARNING("L2CAP resp not queued (work queue full)\n");
        mPendingL2cap.valid = false;
    }
    return true;
}

void AccessoryLink::l2capWorkTrampoline(void *ctx)
{
    static_cast<AccessoryLink *>(ctx)->sendL2capResponse();
}

void AccessoryLink::sendL2capResponse()
{
    PendingL2capReq req = mPendingL2cap;
    mPendingL2cap.valid = false;

    // The link may have dropped between the request and this callback.
    if (!req.valid || req.connHandle != mConnHandle || mConnHandle == 0) {
        return;
    }

    // Policy lives in the band; the arithmetic lives in negotiateConnParams(),
    // which is header-only and host-tested (ConnParamNegotiation_test.cpp).
    // Keeping it out of here is deliberate: this arithmetic is easy to get
    // wrong, and it was untestable while it sat inline behind BlueNRG headers.
    //
    // Note the answer is ALSO what gets applied below, and what the CONTROLLER
    // applies on its own: measured, an accept is followed ~726 ms
    // later by a connection update carrying exactly the answered parameters,
    // with no action from us. So these values are not merely advisory -- they
    // are what the link ends up running, which is why answering and applying
    // must not diverge.
    //
    // The L2CAP response itself carries only a Result field (Core Vol 3 Part A
    // 4.21), so the intervals are never transmitted to the peer; it learns them
    // from LL_CONNECTION_UPDATE_IND. Steering the controller IS how the peer
    // finds out.
    const ConnParamBand band {
        kRelaxIntervalMin, kRelaxIntervalMax, kRelaxLatency,
        kRelaxSupervision, kPeerSupervisionMax,
    };
    const ConnParams wanted {
        req.intervalMin, req.intervalMax, req.latency, req.supTimeout,
    };
    const ConnParamAnswer answer = negotiateConnParams(wanted, band);

    const uint16_t intervalMin = answer.params.intervalMin;
    const uint16_t intervalMax = answer.params.intervalMax;
    const uint16_t latency     = answer.params.latency;
    const uint16_t supTimeout  = answer.params.supTimeout;
    const bool     accept      = answer.accept;

    mCore.lock();
    una_ble_status status = una_ble_l2cap_connection_parameter_update_resp(
            req.connHandle, intervalMin, intervalMax, latency, supTimeout,
            kCeLenMin, kCeLenMax, req.identifier,
            accept ? 0x01 : 0x00);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_WARNING("L2CAP resp failed [0x%02X]\n", status);
        return;
    }
    LOG_INFO("L2CAP conn-param %s: interval %u-%u (%u-%u ms) latency %u "
             "timeout %u (%u ms)\n",
            accept ? "ACCEPTED" : "REJECTED",
            (unsigned)intervalMin, (unsigned)intervalMax,
            (unsigned)((intervalMin * 5u) / 4u),
            (unsigned)((intervalMax * 5u) / 4u),
            (unsigned)latency,
            (unsigned)supTimeout, (unsigned)(supTimeout * 10u));

    if (!accept) {
        return;
    }

    // Apply what we just promised -- but only once streaming.
    //
    // Peers ask EARLY: the COROS sends its request ~100 ms after connect, while
    // GATT discovery is still running, and a connection update in that phase is
    // refused 0x0C command-disallowed (measured on both links).
    // Nothing is lost by skipping it, because relaxConnInterval() runs ~1.6 s
    // later and imposes the very band we just promised -- so the promise is
    // kept by the code that was always going to run.
    //
    // That leaves the apply for the case it exists to cover: a peer that asks
    // AFTER the relax, for something inside our band but narrower.
    //
    // Belt-and-braces either way. Measured, the controller starts its
    // own update from the accept, carrying the answered parameters -- so this
    // call is not what makes the promise true. It stays because it costs one
    // idempotent ACI command and still covers a controller or firmware revision
    // that does not act on the accept.
    if (mState != State::STREAMING) {
        return;
    }

    // Issued unconditionally, with no "already in force" shortcut. Caching what
    // we last REQUESTED would be caching a claim, not a fact: an update accepted
    // at the command level can still fail at the link layer (0x84/0x85, the
    // anchor-slot rejections), and the cache would then assert parameters the
    // link never adopted. A later request resolving to those same values would
    // be answered ACCEPTED and silently skipped -- exactly the "told yes, given
    // something else" condition that makes an H10 fall silent, reintroduced by
    // an optimisation.
    //
    // onConnUpdateComplete() below DOES now observe the completion event, but it
    // is diagnostic only: it records the interval actually in force and warns,
    // and deliberately does not track what was requested. So it does not make
    // the shortcut safe -- resurrecting the cache would need it to reconcile
    // request against outcome, which is a different job. A redundant connection
    // update is cheap and idempotent; correctness is worth one ACI command.
    mCore.lock();
    una_ble_status upd = una_ble_gap_start_connection_update(
            req.connHandle, intervalMin, intervalMax, latency, supTimeout,
            kCeLenMin, kCeLenMax);
    mCore.release();

    if (upd != UNA_BLE_STATUS_SUCCESS) {
        LOG_WARNING("conn update after L2CAP accept failed [0x%02X]\n", upd);
    }
}

}  /* namespace Ble */
