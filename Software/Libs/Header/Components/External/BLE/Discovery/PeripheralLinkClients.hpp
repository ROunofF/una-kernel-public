/**
 ******************************************************************************
 * @file    PeripheralLinkClients.hpp
 * @brief   Serialised GATT-client discovery coordinator for a single BLE link.
 ******************************************************************************
 *
 * Owns the discovery state machine for one link shared by several service
 * clients. Because a link permits only one client procedure at a time, the
 * coordinator discovers registered clients strictly one after another: it finds
 * a client's service, resolves its characteristics, hands the handles to the
 * client for its own subscription / setup writes, and starts the next client
 * only after the current one reports it is ready. Incoming notifications are
 * routed to the client that owns the notifying handle.
 *
 * The coordinator depends only on the IGattClientOps and IGattScheduler ports,
 * so it carries no controller-stack or OS dependency and is host-testable.
 *
 ******************************************************************************
 */

#ifndef BLE_DISCOVERY_PERIPHERAL_LINK_CLIENTS_HPP
#define BLE_DISCOVERY_PERIPHERAL_LINK_CLIENTS_HPP

#include <cstdint>

#include "IGattClientOps.hpp"
#include "IGattScheduler.hpp"
#include "IGattClient.hpp"
#include "IBleThreadWork.hpp"
#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   Drives serialised service discovery for several clients on a link.
 */
class PeripheralLinkClients {
public:

    ///< Maximum number of service clients on one link.
    static constexpr uint8_t kMaxClients = 4;

    PeripheralLinkClients() = default;

    /**
     * @brief   Convenience constructor that binds the outbound ports.
     */
    PeripheralLinkClients(IGattClientOps& ops, IGattScheduler& sched)
    {
        bind(ops, sched);
    }

    /**
     * @brief   Bind the outbound ports (called once by the link owner).
     * @param   ops: GATT operations used to discover and write.
     * @param   sched: one-shot scheduler used to pace discovery steps.
     */
    void bind(IGattClientOps& ops, IGattScheduler& sched)
    {
        mpOps = &ops;
        mpSched = &sched;
    }

    /**
     * @brief   Register a service client (call before the link is up).
     * @param   client: client to discover and route to. Ignored if full.
     */
    void registerClient(IGattClient* client);

    /**
     * @brief   The outbound GATT operations port (used by clients during setup).
     */
    IGattClientOps& ops() const { return *mpOps; }

    /**
     * @brief   The connection handle currently being served.
     */
    uint16_t connHandle() const { return mConnHandle; }

    /**
     * @brief   Whether all registered clients have finished discovery.
     * @note    A link permits one client procedure at a time; clients must gate
     *          their runtime writes on this so they do not collide with a
     *          still-running discovery of another client.
     */
    bool isReady() const { return mState == State::DONE; }

    /**
     * @brief   Register the link (its IBle::isTransferQuiet reports activity).
     */
    void setLinkActivity(Interface::IBle* activity) { mActivity = activity; }

    /**
     * @brief   Register the port that runs work on the BLE event thread.
     * @note    Clients use postToBleThread() to marshal their timer-driven setup
     *          steps onto that thread; without it those steps are dropped.
     */
    void setBleThreadWork(IBleThreadWork* work) { mpWork = work; }

    /**
     * @brief   Run a callback on the BLE event thread (used by clients so their
     *          timer-driven setup steps do not race the event thread).
     * @retval  'true' if queued, 'false' if unregistered or the queue is full.
     */
    bool postToBleThread(void (*fn)(void*), void* ctx)
    {
        return mpWork != nullptr && mpWork->post(fn, ctx);
    }

    /**
     * @brief   Whether the link has been quiet for at least quietMs.
     * @note    Returns 'true' if no activity source is registered. Clients use
     *          this to defer non-essential writes during a busy transfer.
     */
    bool transferQuiet(uint32_t quietMs) const
    {
        return mActivity == nullptr || mActivity->isTransferQuiet(quietMs);
    }

    /**
     * @brief   Begin discovery on a freshly established/encrypted link.
     * @param   connHandle: connection to discover on.
     */
    void start(uint16_t connHandle);

    /**
     * @brief   Drop all per-link state on disconnection or coprocessor reset.
     */
    void reset();

    /**
     * @brief   Signalled by the current client once its setup writes are done.
     */
    void clientReady();

    /**
     * @brief   Scheduler tick: run the pending delayed discovery step.
     */
    void onTick();

    // --- Routed link events (delivered by the link owner) -----------------

    void onServiceGroupFound(uint16_t foundHandle, uint16_t endHandle);
    void onCharDiscovered(const uint8_t* attrData, uint8_t len);
    void onCharDescFound(uint16_t attrHandle, uint16_t uuid16);
    void onGattError(uint8_t reqOpcode, uint16_t attrHandle, uint8_t errorCode);
    void onGattProcComplete(uint8_t errorCode);
    void onNotification(uint16_t attrHandle, const uint8_t* data, uint8_t len);

private:

    enum class State {
        IDLE = 0,
        DELAYED_SRV_DISCOVERY,
        SRV_DISCOVERED,
        DELAYED_CHR_DISCOVERY,
        CHR_DISCOVERED,
        DELAYED_DESC_DISCOVERY,
        AWAIT_CLIENT_READY,
        DONE
    };

    ///< Controller settle delay before the first service discovery.
    static constexpr uint32_t kSettleMs = 1000;
    ///< Delay between discovery stages and between clients.
    static constexpr uint32_t kStageMs = 100;
    ///< Delay before retrying a discovery request the controller rejected.
    static constexpr uint32_t kDiscRetryMs = 500;
    ///< Bounded retries for a discovery request (controller momentarily busy).
    static constexpr uint8_t kMaxDiscRetry = 6;
    ///< Quiet window the link must show before a discovery step is issued, so it
    ///< does not fight a burst of app traffic right after connect.
    static constexpr uint32_t kQuietMs = 1500;
    ///< Poll interval while waiting for the link to become quiet.
    static constexpr uint32_t kQuietPollMs = 500;
    ///< Bound on how long to wait for quiet before proceeding anyway.
    static constexpr uint8_t kMaxQuietWaits = 60;
    ///< Watchdog for a stage awaiting its controller completion event (ATT 0x0A
    ///< or procedure-complete), which heavy link traffic can delay or drop. On
    ///< expiry the coordinator forces progress so discovery never hangs.
    static constexpr uint32_t kStageTimeoutMs = 2000;
    ///< Watchdog for a client that resolved but never reports it is ready (its
    ///< setup stalled). On expiry the coordinator skips it so the link does not
    ///< stay blocked for the whole connection. Sized well above a normal setup.
    static constexpr uint32_t kClientReadyTimeoutMs = 10000;
    ///< ATT "Attribute Not Found": the discovery stage has completed.
    static constexpr uint8_t kAttErrNotFound = 0x0A;
    ///< Length of a 128-bit handle/value pair in a read-by-type response.
    static constexpr uint8_t kCharPair128Len = 21;
    ///< Client Characteristic Configuration descriptor UUID.
    static constexpr uint16_t kCccdUuid = 0x2902;

    IGattClientOps* mpOps { nullptr };
    IGattScheduler* mpSched { nullptr };
    Interface::IBle* mActivity { nullptr };
    IBleThreadWork* mpWork { nullptr };

    IGattClient* mClients[kMaxClients] {};
    uint8_t mClientCount {};

    // Resolved services kept for the link's lifetime so notifications route
    // even while a later client is still being discovered.
    ResolvedService mResolved[kMaxClients] {};

    State mState { State::IDLE };
    uint8_t mCurrent {};        ///< Index of the client being discovered.
    uint8_t mDiscRetry {};      ///< Retry count for the current discovery request.
    uint8_t mQuietWaits {};     ///< Polls spent waiting for the link to go quiet.
    bool mInDescDiscovery {};   ///< A descriptor-discovery procedure is in flight.
    uint16_t mConnHandle {};
    uint16_t mStartHandle {};
    uint16_t mEndHandle {};

    void beginClient(uint32_t delayMs);
    void advance();
    void serviceDiscovery();
    void characteristicsDiscovery();
    void descriptorDiscovery();
    void resolveCurrent();
    // Advance a "found, awaiting completion" stage (SRV_DISCOVERED ->
    // characteristics, CHR_DISCOVERED -> descriptors). Shared by the 0x0A and
    // procedure-complete completion events and by the stage watchdog.
    bool completeFoundStage();
};

/**
 * @brief   The process-wide phone-link GATT-client coordinator.
 * @note    The link owner binds its ports (bind / setLinkActivity); service
 *          clients register on it and are consumed directly, so neither the
 *          link owner nor the interface needs to know the concrete clients.
 */
PeripheralLinkClients& blePeripheralClients();

} // namespace Ble

#endif // BLE_DISCOVERY_PERIPHERAL_LINK_CLIENTS_HPP
