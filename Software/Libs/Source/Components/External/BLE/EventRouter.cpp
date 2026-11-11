/**
 ******************************************************************************
 * @file    EventRouter.cpp
 * @brief   Routes BlueNRG link events to the owning Ble::ILinkClient.
 ******************************************************************************
 *
 * Pure routing logic only: no BlueNRG or OS dependency, so it is compiled and
 * exercised directly by the host unit tests. The global C event handlers that
 * feed it live in BleEventDispatch.cpp.
 *
 ******************************************************************************
 */

#include "EventRouter.hpp"

namespace Ble
{

void EventRouter::setRoleClient(LinkRole role, ILinkClient *client)
{
    uint8_t idx = static_cast<uint8_t>(role);
    if (idx >= kRoleCount) {
        return;
    }
    mRoleClients[idx] = client;
}

ILinkClient* EventRouter::roleClient(LinkRole role) const
{
    // Bounds-check: the role comes from the controller's connection-complete
    // Role field (spec 0x00/0x01) but is otherwise an external byte indexing a
    // fixed array — guard against an out-of-range value (latent OOB read).
    uint8_t idx = static_cast<uint8_t>(role);
    if (idx >= kRoleCount) {
        return nullptr;
    }
    return mRoleClients[idx];
}

ILinkClient* EventRouter::clientFor(uint16_t connHandle) const
{
    for (const Binding &b : mLinks) {
        if (b.used && b.handle == connHandle) {
            return b.client;
        }
    }
    return nullptr;
}

uint8_t EventRouter::linkCount() const
{
    uint8_t n = 0;
    for (const Binding &b : mLinks) {
        if (b.used) {
            ++n;
        }
    }
    return n;
}

void EventRouter::reset()
{
    for (Binding &b : mLinks) {
        b = Binding {};
    }
    mRoleClients[0] = nullptr;
    mRoleClients[1] = nullptr;
}

bool EventRouter::bind(uint16_t connHandle, ILinkClient *client)
{
    // Replace an existing binding for this handle if one is present (the
    // controller never reuses a handle for two live links, but a stale binding
    // from a missed disconnection must not shadow the new one).
    for (Binding &b : mLinks) {
        if (b.used && b.handle == connHandle) {
            b.client = client;
            return true;
        }
    }
    for (Binding &b : mLinks) {
        if (!b.used) {
            b.used = true;
            b.handle = connHandle;
            b.client = client;
            return true;
        }
    }
    // Table full (more than kMaxLinks concurrent links). Not reachable with one
    // phone link + the bounded accessory path, but signal it so the caller does
    // not report a connection it can never route the disconnection for.
    return false;
}

void EventRouter::unbind(uint16_t connHandle)
{
    for (Binding &b : mLinks) {
        if (b.used && b.handle == connHandle) {
            b = Binding {};
            return;
        }
    }
}

bool EventRouter::onConnectionComplete(LinkRole role, uint16_t connHandle,
        uint8_t peerAddrType, const uint8_t peerAddr[6], uint16_t interval,
        uint16_t latency, uint16_t supTimeout)
{
    ILinkClient *client = roleClient(role);
    if (client == nullptr) {
        return false;
    }

    // Only notify the owner if the handle was actually bound — otherwise its
    // later per-handle events (incl. disconnection) wouldn't route, leaving the
    // owner believing it is connected with no way to learn of the drop.
    if (!bind(connHandle, client)) {
        return false;
    }

    client->onConnected(connHandle, peerAddrType, peerAddr, interval, latency,
            supTimeout);
    return true;
}

void EventRouter::onDisconnection(uint16_t connHandle, uint8_t reason)
{
    ILinkClient *client = clientFor(connHandle);
    if (client != nullptr) {
        client->onDisconnected(connHandle, reason);
    }
    unbind(connHandle);
}

void EventRouter::onServiceGroupFound(uint16_t connHandle, uint16_t foundHandle,
        uint16_t endHandle)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onServiceGroupFound(connHandle, foundHandle, endHandle);
    }
}

void EventRouter::onCharDiscovered(uint16_t connHandle, const uint8_t *attrData,
        uint8_t len)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onCharDiscovered(connHandle, attrData, len);
    }
}

void EventRouter::onCharDescFound(uint16_t connHandle, uint16_t attrHandle,
        uint16_t uuid16)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onCharDescFound(connHandle, attrHandle, uuid16);
    }
}

void EventRouter::onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *value, uint8_t len)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onCharByUuidResp(connHandle, attrHandle, value, len);
    }
}

void EventRouter::onNotification(uint16_t connHandle, uint16_t attrHandle,
        const uint8_t *data, uint8_t len)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onNotification(connHandle, attrHandle, data, len);
    }
}

void EventRouter::onConnUpdateComplete(uint16_t connHandle, uint8_t status,
        uint16_t interval, uint16_t latency, uint16_t supTimeout)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onConnUpdateComplete(connHandle, status, interval, latency,
                supTimeout);
    }
}

void EventRouter::onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
        uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
        uint16_t supTimeout)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onL2capUpdateReq(connHandle, identifier, intervalMin, intervalMax,
                latency, supTimeout);
    }
}

void EventRouter::onGattProcComplete(uint16_t connHandle, uint8_t errorCode)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onGattProcComplete(connHandle, errorCode);
    }
}

void EventRouter::onGattError(uint16_t connHandle, uint8_t reqOpcode,
        uint16_t attrHandle, uint8_t errorCode)
{
    if (ILinkClient *c = clientFor(connHandle)) {
        c->onGattError(connHandle, reqOpcode, attrHandle, errorCode);
    }
}

void EventRouter::onAdvReport(uint8_t addrType, const uint8_t addr[6],
        uint8_t dataLen, const uint8_t *data, int8_t rssi, uint8_t eventType)
{
    // Advertising reports only fire during a central scan, so deliver to the
    // central-role client (the accessory link owner).
    if (ILinkClient *c = roleClient(LinkRole::Central)) {
        c->onAdvReport(addrType, addr, dataLen, data, rssi, eventType);
    }
}

void EventRouter::onGapProcComplete(uint8_t procedureCode, uint8_t status,
        const uint8_t *data, uint8_t len)
{
    // GAP scan/connect/discovery procedures are central-initiated, so deliver
    // completion to the central-role client (the accessory link owner).
    if (ILinkClient *c = roleClient(LinkRole::Central)) {
        c->onGapProcComplete(procedureCode, status, data, len);
    }
}

EventRouter& bleEventRouter()
{
    static EventRouter sRouter;
    return sRouter;
}

}  /* namespace Ble */
