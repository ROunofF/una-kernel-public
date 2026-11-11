/**
 ******************************************************************************
 * @file    AccessoryLink.hpp
 * @brief   One central-role connection to an accessory, from connect to stream.
 ******************************************************************************
 *
 * Owns everything that is per-PEER: the connect -> discover -> subscribe -> tune
 * -> stream state machine, the connection handle, the peer address and name, the
 * profile that decodes its notifications, and its own timer. Everything that is
 * per-RADIO or per-SESSION -- the scan, the scan-cycle budget, the fast/slow
 * re-acquire ladder, the scan-start back-off, the remembered-device allow-list --
 * stays with the supervisor (AccessoryCentral), which owns N of these.
 *
 * The seam is deliberately an OBSERVER CONTRACT rather than shared state. In the
 * single-link code the link's failure paths reached directly into scan policy
 * (a dropped link reset the scan budget and re-armed scanning inline). With
 * several links that is wrong: one peer disconnecting must not silently reset a
 * budget another peer is relying on. So a link reports what happened -- closed,
 * status changed, sample decoded -- and the supervisor decides what it means.
 *
 * EXECUTION MODEL (unchanged, and load-bearing). BLE event handlers only record
 * state and arm mTimer; every ACI call is issued from the timer callback, where
 * taking the core mutex is safe (never lock across a wait). All links' timer
 * callbacks run in FreeRTOS's single timer-service task, which serialises ACI
 * work across links for free -- that is what keeps the one-GAP-procedure and
 * one-GATT-procedure-per-connection rules satisfied without extra locking. The
 * corollary is that a link must never block in its callback: it would stall every
 * other link and every other software timer in the system.
 *
 * ONE EXCEPTION. sendL2capResponse() issues its ACI calls from
 * the BLE work queue -- CoreBlueNRG's HCI service thread -- not from the timer
 * callback, because the request that triggers it arrives as a BlueNRG event and
 * answering it from event context would let the event pump consume its own
 * command's response. That means its una_ble_l2cap_connection_parameter_update_resp
 * and una_ble_gap_start_connection_update can overlap an ACI call made from the
 * timer thread (relaxConnInterval() being the realistic one), and mState /
 * mConnHandle are read there cross-thread. In practice the loser of such a race
 * gets a status code and a warning -- the core mutex still serialises the
 * transport itself -- but the "single timer task serialises everything"
 * invariant above no longer covers the whole class. Anyone adding another ACI
 * call must decide which thread it belongs on rather than assuming the timer.
 *
 ******************************************************************************
 */

#ifndef ACCESSORY_LINK_HPP
#define ACCESSORY_LINK_HPP

#include <cstdint>

#include "OS.hpp"
#include "CoreBlueNRG.hpp"
#include "IBle.hpp"
#include "IAccessoryProfile.hpp"

namespace Ble
{

/**
 * @brief   One accessory connection and the profile that decodes it.
 */
class AccessoryLink {
public:

    /**
     * @brief   Coarse per-link progress, reported to the supervisor.
     */
    enum class Status : uint8_t {
        Idle,        ///< no peer assigned
        Connecting,  ///< connecting / discovering / subscribing
        Streaming,   ///< notifications flowing
        Closed,      ///< peer gone or the attempt was abandoned
    };

    /**
     * @brief   Why a link closed, so the supervisor can pick a policy.
     */
    enum class CloseReason : uint8_t {
        Disconnected,     ///< peer dropped the link (or we dropped a bad one)
        ConnectFailed,    ///< the controller refused or the attempt timed out
        DiscoveryFailed,  ///< characteristic discovery/subscribe gave up
        Released,         ///< torn down deliberately
    };

    /**
     * @brief   Sink for one link's events. Implemented by the supervisor.
     */
    class Observer {
    public:
        /// A notification decoded into a typed sample.
        virtual void onLinkSample(AccessoryLink &link,
                const AccessorySample &sample) = 0;
        /// Coarse progress changed (drives the app/GUI status bridge).
        virtual void onLinkStatus(AccessoryLink &link, Status status) = 0;
        /**
         * @brief   The link is no longer usable and the slot is free.
         * @param   everStreamed: whether this peer ever streamed. The supervisor
         *          needs it to choose between "re-acquire, it will come back"
         *          and "give up, we never had it" -- the distinction that keeps
         *          a first acquisition bounded but a re-acquisition patient.
         */
        virtual void onLinkClosed(AccessoryLink &link, CloseReason reason,
                bool everStreamed) = 0;
    protected:
        virtual ~Observer() = default;
    };

    /**
     * @brief   Construct an idle link.
     * @param   core: BLE core (ACI access + mutex).
     * @param   profile: the GATT profile this link decodes.
     * @param   phone: phone link, for the transfer-quiet coexistence gate.
     * @param   observer: sink for samples, status and closure.
     */
    AccessoryLink(CoreBlueNRG &core, IAccessoryProfile &profile,
            Interface::IBle &phone, Observer &observer);

    /**
     * @brief   Create the link's timer. Separate from the constructor so the
     *          caller can detect and log RTOS-heap exhaustion: osTimerNew
     *          allocates, and a link with a dead timer would silently never
     *          progress past connect.
     * @param   name: timer name for RTOS introspection.
     * @retval  'false' if the timer could not be created.
     */
    bool init(const char *name);

    /**
     * @brief   Begin connecting to a peer found by the scanner.
     * @note    The caller must have stopped scanning first: GAP connect and GAP
     *          discovery are the same global resource.
     */
    void begin(const uint8_t addr[6], uint8_t addrType, const char *name);

    /**
     * @brief   Tear the link down and return to Idle. Safe in any state.
     */
    void close();

    bool isIdle() const { return mState == State::IDLE; }
    bool isStreaming() const { return mState == State::STREAMING; }
    uint16_t connHandle() const { return mConnHandle; }
    const char *peerName() const { return mPeerName; }
    const uint8_t *peerAddr() const { return mPeerAddr; }
    IAccessoryProfile &profile() { return mProfile; }

    // --- events routed here by the supervisor -----------------------------
    /// @retval 'true' if this link owns the connection and consumed the event.
    bool onConnected(uint16_t connHandle, uint16_t interval);
    bool onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *value, uint8_t len);
    bool onGattProcComplete(uint16_t connHandle, uint8_t errorCode);
    bool onNotification(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *data, uint8_t len);
    bool onDisconnected(uint16_t connHandle, uint8_t reason);
    bool onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
            uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
            uint16_t supTimeout);
    bool onConnUpdateComplete(uint16_t connHandle, uint8_t status,
            uint16_t interval, uint16_t latency, uint16_t supTimeout);


private:

    // Post-handover states only; scanning belongs to the supervisor.
    enum class State : uint8_t {
        IDLE,
        CONNECT_PENDING,
        CONNECTING,
        DISCOVER_PENDING,
        DISCOVERING,
        DISCOVERED,
        SUBSCRIBE_PENDING,
        TUNING_PENDING,
        STREAMING,
    };

    void setState(State next);
    void timCallback(void *arg);

    void createConnection();
    void discoverCharacteristic();
    void enableNotifications();
    void relaxConnInterval();
    // Connection-parameter policy arithmetic lives in ConnParamNegotiation.hpp.
    /**
     * @brief   Terminate the link, recording why so onDisconnected() can report
     *          it honestly rather than as a peer-initiated drop.
     * @param   reason: what the resulting closure means to the supervisor.
     * @retval  'false' if the controller refused the terminate -- no
     *          disconnection event will follow, so the caller must close its
     *          own state or the link wedges.
     */
    bool dropLink(CloseReason reason);
    void finish(CloseReason reason);

    /**
     * @brief   Send the queued L2CAP connection-parameter response.
     * @note    Runs on the BLE work queue, NOT in event context. CoreBlueNRG's
     *          mutex IS recursive, so an inline call would not deadlock -- the
     *          hazard is different and worse. Event callbacks run inside
     *          una_ble_hci_user_evt_proc(), which holds that mutex precisely so the
     *          event thread cannot intercept a command's own response; issuing
     *          a command from there invites the pump to consume its own reply.
     *          It also blocks event draining for the command's whole wait (up
     *          to the HCI timeout), which is how the controller's event queue
     *          overflows into aci_blue_events_lost. Do not "simplify" this
     *          deferral away on the grounds that no deadlock occurs.
     */
    void sendL2capResponse();
    static void l2capWorkTrampoline(void *ctx);

    /// A peer's parameter request, held between event context and the work queue.
    struct PendingL2capReq {
        uint16_t connHandle { 0 };
        uint16_t intervalMin { 0 };
        uint16_t intervalMax { 0 };
        uint16_t latency { 0 };
        uint16_t supTimeout { 0 };
        uint8_t  identifier { 0 };
        bool     valid { false };
    };

    CoreBlueNRG       &mCore;
    IAccessoryProfile &mProfile;
    Interface::IBle   &mPhone;
    Observer          &mObserver;

    OS::Timer<AccessoryLink> mTimer;

    State    mState { State::IDLE };
    uint8_t  mPeerAddr[6] { };
    uint8_t  mPeerAddrType { 0 };
    char     mPeerName[24] { };
    uint16_t mConnHandle { 0 };
    uint8_t  mDiscoverRetries { 0 };
    bool     mEverStreamed { false };
    Status   mLastStatus { Status::Idle };
    PendingL2capReq mPendingL2cap { };
    uint16_t mActualInterval { 0 };
    /// Why the next closure happened. Disconnected unless dropLink() sets it,
    /// so a genuine peer drop still resets the supervisor's scan budget.
    CloseReason mDropReason { CloseReason::Disconnected };
};

}  /* namespace Ble */

#endif /* ACCESSORY_LINK_HPP */
