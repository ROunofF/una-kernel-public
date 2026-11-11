/**
 ******************************************************************************
 * @file    EventRouter.hpp
 * @brief   Routes BlueNRG link events to the owning Ble::ILinkClient.
 ******************************************************************************
 *
 * The BlueNRG-2 stack delivers every event through a single set of global C
 * callbacks, with no notion of which logical link an event belongs to. With
 * more than one link in flight (phone peripheral link + accessory central
 * link) each event must be steered to the correct owner:
 *
 *  - connection-complete is routed by **role** (controller Role field):
 *    peripheral connections go to the phone-link owner, central connections to
 *    the accessory-link owner. The handle is then bound to that owner.
 *  - subsequent per-link events are routed by **connection handle** to the
 *    bound owner.
 *  - the central-only GAP-procedure-complete event (which carries no handle)
 *    is routed to the central-role client.
 *
 * The router is deliberately free of BlueNRG and OS dependencies: the global C
 * handlers (BleEventDispatch.cpp) translate stack payloads into these plain
 * calls, and the routing table is exercised directly by host unit tests.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_EVENT_ROUTER_HPP
#define __COMPONENT_BLE_EVENT_ROUTER_HPP

#include <cstdint>

#include "ILinkClient.hpp"

namespace Ble
{

/**
 * @brief   BlueNRG link role, matching the controller Role field values.
 */
enum class LinkRole : uint8_t {
    Central    = 0x00,  // watch initiated the connection (e.g. to an accessory)
    Peripheral = 0x01,  // watch was connected to (e.g. by the phone)
};

/**
 * @brief   Steers routed BlueNRG events to the owning ILinkClient.
 *
 * Thread-safety: all routing methods (onConnectionComplete/onDisconnection/
 * on*) run on the BlueNRG event-processing thread, per the ILinkClient
 * contract. The only access off that thread is setRoleClient(), called once
 * each at init/teardown (PeripheralBlueNRG ctor/dtor) when no events are in
 * flight — so no locking is needed.
 */
class EventRouter {
public:

    // Concurrent link ceiling. One phone link + headroom for accessory links
    // (slot budget). Bindings beyond this are dropped.
    static constexpr uint8_t kMaxLinks = 4;

    /**
     * @brief   Register the client that owns links of the given role.
     * @param   role: link role this client owns.
     * @param   client: owner, or nullptr to clear.
     */
    void setRoleClient(LinkRole role, ILinkClient *client);

    /**
     * @brief   Get the client registered for a role.
     * @param   role: link role.
     * @retval  Registered client, or nullptr.
     */
    ILinkClient* roleClient(LinkRole role) const;

    /**
     * @brief   Get the client currently bound to a connection handle.
     * @param   connHandle: connection handle.
     * @retval  Bound client, or nullptr if the handle is not bound.
     */
    ILinkClient* clientFor(uint16_t connHandle) const;

    /**
     * @brief   Number of currently bound connection handles.
     */
    uint8_t linkCount() const;

    /**
     * @brief   Forget all bindings and registered role clients.
     */
    void reset();

    // --- Routed events (called by the global C handlers) -------------------

    /**
     * @brief   A connection completed: bind its handle to the role's owner and
     *          notify that owner.
     * @retval  'true' if the link was bound and its owner notified; 'false' if
     *          no client owns the role (incl. an out-of-range role byte) or the
     *          link table is full — in which case onConnected() is NOT called,
     *          so the owner never sees a connection it can't later be told
     *          about.
     */
    bool onConnectionComplete(LinkRole role, uint16_t connHandle,
            uint8_t peerAddrType, const uint8_t peerAddr[6], uint16_t interval,
            uint16_t latency, uint16_t supTimeout);

    /**
     * @brief   A connection dropped: notify the bound owner and unbind.
     */
    void onDisconnection(uint16_t connHandle, uint8_t reason);

    void onServiceGroupFound(uint16_t connHandle, uint16_t foundHandle,
            uint16_t endHandle);
    void onCharDiscovered(uint16_t connHandle, const uint8_t *attrData,
            uint8_t len);
    void onCharDescFound(uint16_t connHandle, uint16_t attrHandle,
            uint16_t uuid16);
    void onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *value, uint8_t len);
    void onNotification(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *data, uint8_t len);
    void onConnUpdateComplete(uint16_t connHandle, uint8_t status,
            uint16_t interval, uint16_t latency, uint16_t supTimeout);
    void onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
            uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
            uint16_t supTimeout);
    void onGattProcComplete(uint16_t connHandle, uint8_t errorCode);
    void onGattError(uint16_t connHandle, uint8_t reqOpcode,
            uint16_t attrHandle, uint8_t errorCode);

    /**
     * @brief   An advertising report from a central scan. Routed to the
     *          central-role client (no connection handle yet).
     */
    void onAdvReport(uint8_t addrType, const uint8_t addr[6], uint8_t dataLen,
            const uint8_t *data, int8_t rssi, uint8_t eventType);

    /**
     * @brief   A central-initiated GAP procedure completed. Routed to the
     *          central-role client (it carries no connection handle).
     */
    void onGapProcComplete(uint8_t procedureCode, uint8_t status,
            const uint8_t *data, uint8_t len);

private:

    struct Binding {
        bool         used   { false };
        uint16_t     handle { 0 };
        ILinkClient *client { nullptr };
    };

    // Number of LinkRole values (Central = 0, Peripheral = 1). roleClient()/
    // setRoleClient() bounds-check against this so a controller-supplied role
    // byte can never index out of range.
    static constexpr uint8_t kRoleCount = 2;

    Binding mLinks[kMaxLinks] {};
    ILinkClient *mRoleClients[kRoleCount] { nullptr, nullptr };

    // Binds connHandle -> client. Returns false if the table is full.
    bool bind(uint16_t connHandle, ILinkClient *client);
    void unbind(uint16_t connHandle);
};

/**
 * @brief   The single process-wide router used by the global C event handlers.
 */
EventRouter& bleEventRouter();

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_EVENT_ROUTER_HPP */
