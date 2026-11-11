/**
 * Host unit tests for Ble::EventRouter — the handle/role dispatch table that
 * steers BlueNRG link events to the owning ILinkClient.
 *
 * Pure logic: no BlueNRG/OS dependency, so the routing table is exercised
 * directly. The global C handlers that translate stack payloads into these
 * calls live in BleEventDispatch.cpp and are covered by on-hardware tests.
 */

#include <gtest/gtest.h>

#include "Components/External/BLE/EventRouter.hpp"

namespace {

using Ble::EventRouter;
using Ble::ILinkClient;
using Ble::LinkRole;

// Recording spy: counts each routed callback and remembers the last arguments
// so a test can assert both that an event landed and that it landed here.
class SpyClient : public ILinkClient {
public:
    int connected { 0 };
    int disconnected { 0 };
    int serviceGroup { 0 };
    int charDiscovered { 0 };
    int charByUuid { 0 };
    int notification { 0 };
    int procComplete { 0 };
    int gattError { 0 };
    int gapProc { 0 };

    uint16_t lastConnHandle { 0 };
    uint16_t lastAttrHandle { 0 };
    uint8_t  lastReason { 0 };
    uint8_t  lastErrorCode { 0 };
    uint8_t  lastProcedureCode { 0 };

    void onConnected(uint16_t connHandle, uint8_t, const uint8_t[6], uint16_t,
            uint16_t, uint16_t) override
    {
        ++connected;
        lastConnHandle = connHandle;
    }
    void onDisconnected(uint16_t connHandle, uint8_t reason) override
    {
        ++disconnected;
        lastConnHandle = connHandle;
        lastReason = reason;
    }
    void onServiceGroupFound(uint16_t connHandle, uint16_t, uint16_t) override
    {
        ++serviceGroup;
        lastConnHandle = connHandle;
    }
    void onCharDiscovered(uint16_t connHandle, const uint8_t *, uint8_t) override
    {
        ++charDiscovered;
        lastConnHandle = connHandle;
    }
    void onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *, uint8_t) override
    {
        ++charByUuid;
        lastConnHandle = connHandle;
        lastAttrHandle = attrHandle;
    }
    void onNotification(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *, uint8_t) override
    {
        ++notification;
        lastConnHandle = connHandle;
        lastAttrHandle = attrHandle;
    }
    void onGattProcComplete(uint16_t connHandle, uint8_t errorCode) override
    {
        ++procComplete;
        lastConnHandle = connHandle;
        lastErrorCode = errorCode;
    }
    void onGattError(uint16_t connHandle, uint8_t, uint16_t, uint8_t errorCode)
            override
    {
        ++gattError;
        lastConnHandle = connHandle;
        lastErrorCode = errorCode;
    }
    void onGapProcComplete(uint8_t procedureCode, uint8_t, const uint8_t *,
            uint8_t) override
    {
        ++gapProc;
        lastProcedureCode = procedureCode;
    }
};

const uint8_t kAddr[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

void connectPeripheral(EventRouter &r, uint16_t handle)
{
    r.onConnectionComplete(LinkRole::Peripheral, handle, 0x01, kAddr, 24, 0, 400);
}

} // namespace

TEST(EventRouter, RoleClientRegistration)
{
    EventRouter r;
    SpyClient phone;
    EXPECT_EQ(r.roleClient(LinkRole::Peripheral), nullptr);

    r.setRoleClient(LinkRole::Peripheral, &phone);
    EXPECT_EQ(r.roleClient(LinkRole::Peripheral), &phone);
    EXPECT_EQ(r.roleClient(LinkRole::Central), nullptr);
}

TEST(EventRouter, ConnectionBindsHandleToRoleOwner)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);

    connectPeripheral(r, 0x0040);

    EXPECT_EQ(phone.connected, 1);
    EXPECT_EQ(phone.lastConnHandle, 0x0040);
    EXPECT_EQ(r.clientFor(0x0040), &phone);
    EXPECT_EQ(r.linkCount(), 1);
}

TEST(EventRouter, ConnectionWithNoRoleOwnerIsDropped)
{
    EventRouter r; // no clients registered
    r.onConnectionComplete(LinkRole::Central, 0x0040, 0x00, kAddr, 24, 0, 400);

    EXPECT_EQ(r.clientFor(0x0040), nullptr);
    EXPECT_EQ(r.linkCount(), 0);
}

TEST(EventRouter, PerHandleEventsDispatchToBoundClient)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    connectPeripheral(r, 0x0040);

    const uint8_t payload[2] = { 0xAA, 0xBB };
    r.onServiceGroupFound(0x0040, 0x0010, 0x0020);
    r.onCharDiscovered(0x0040, payload, sizeof(payload));
    r.onCharByUuidResp(0x0040, 0x0015, payload, sizeof(payload));
    r.onNotification(0x0040, 0x0012, payload, sizeof(payload));
    r.onGattProcComplete(0x0040, 0x00);
    r.onGattError(0x0040, 0x08, 0x0012, 0x0A);

    EXPECT_EQ(phone.serviceGroup, 1);
    EXPECT_EQ(phone.charDiscovered, 1);
    EXPECT_EQ(phone.charByUuid, 1);
    EXPECT_EQ(phone.notification, 1);
    EXPECT_EQ(phone.procComplete, 1);
    EXPECT_EQ(phone.gattError, 1);
    EXPECT_EQ(phone.lastAttrHandle, 0x0012);
    EXPECT_EQ(phone.lastErrorCode, 0x0A);
}

TEST(EventRouter, EventsForUnboundHandleAreIgnored)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    connectPeripheral(r, 0x0040);

    const uint8_t payload[1] = { 0x01 };
    r.onNotification(0x0099, 0x0012, payload, sizeof(payload)); // wrong handle

    EXPECT_EQ(phone.notification, 0);
}

TEST(EventRouter, TwoRolesRouteIndependentlyByHandle)
{
    EventRouter r;
    SpyClient phone;
    SpyClient strap;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    r.setRoleClient(LinkRole::Central, &strap);

    connectPeripheral(r, 0x0040);
    r.onConnectionComplete(LinkRole::Central, 0x0041, 0x00, kAddr, 12, 0, 200);
    EXPECT_EQ(r.linkCount(), 2);

    const uint8_t payload[1] = { 0x7F };
    r.onNotification(0x0041, 0x0030, payload, sizeof(payload));

    EXPECT_EQ(strap.notification, 1);
    EXPECT_EQ(strap.lastConnHandle, 0x0041);
    EXPECT_EQ(phone.notification, 0);
}

TEST(EventRouter, DisconnectionNotifiesOwnerAndUnbinds)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    connectPeripheral(r, 0x0040);

    r.onDisconnection(0x0040, 0x13);

    EXPECT_EQ(phone.disconnected, 1);
    EXPECT_EQ(phone.lastReason, 0x13);
    EXPECT_EQ(r.clientFor(0x0040), nullptr);
    EXPECT_EQ(r.linkCount(), 0);
}

TEST(EventRouter, DisconnectionOfUnboundHandleIsNoOp)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);

    r.onDisconnection(0x0040, 0x13); // never connected

    EXPECT_EQ(phone.disconnected, 0);
    EXPECT_EQ(r.linkCount(), 0);
}

TEST(EventRouter, GapProcCompleteRoutesToCentralClientOnly)
{
    EventRouter r;
    SpyClient phone;
    SpyClient strap;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    r.setRoleClient(LinkRole::Central, &strap);

    r.onGapProcComplete(0x04 /* scan */, 0x00, nullptr, 0);

    EXPECT_EQ(strap.gapProc, 1);
    EXPECT_EQ(strap.lastProcedureCode, 0x04);
    EXPECT_EQ(phone.gapProc, 0);
}

TEST(EventRouter, GapProcCompleteWithoutCentralClientIsNoOp)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);

    r.onGapProcComplete(0x04, 0x00, nullptr, 0); // no central client

    EXPECT_EQ(phone.gapProc, 0);
}

TEST(EventRouter, StaleBindingForReusedHandleIsReplaced)
{
    EventRouter r;
    SpyClient first;
    SpyClient second;
    r.setRoleClient(LinkRole::Peripheral, &first);

    connectPeripheral(r, 0x0040);
    // Same handle reconnects before a disconnection was seen; the owner for
    // the role is now a different client. The new binding must win.
    r.setRoleClient(LinkRole::Peripheral, &second);
    connectPeripheral(r, 0x0040);

    EXPECT_EQ(r.clientFor(0x0040), &second);
    EXPECT_EQ(r.linkCount(), 1);
}

TEST(EventRouter, ResetClearsBindingsAndRoleClients)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);
    connectPeripheral(r, 0x0040);

    r.reset();

    EXPECT_EQ(r.linkCount(), 0);
    EXPECT_EQ(r.clientFor(0x0040), nullptr);
    EXPECT_EQ(r.roleClient(LinkRole::Peripheral), nullptr);
}

TEST(EventRouter, TableFullDropsConnectionWithoutNotifying)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);

    // Fill every link slot (distinct handles, same role owner).
    const int max = EventRouter::kMaxLinks;
    for (int i = 0; i < max; ++i) {
        EXPECT_TRUE(r.onConnectionComplete(LinkRole::Peripheral,
                static_cast<uint16_t>(0x0040 + i), 0x01, kAddr, 24, 0, 400));
    }
    EXPECT_EQ(static_cast<int>(r.linkCount()), max);
    EXPECT_EQ(phone.connected, max);

    // One more must be dropped: not bound, and onConnected NOT called — else
    // the owner would believe it is connected with no way to learn of the drop.
    const uint16_t overflow = static_cast<uint16_t>(0x0040 + max);
    EXPECT_FALSE(r.onConnectionComplete(LinkRole::Peripheral, overflow,
            0x01, kAddr, 24, 0, 400));
    EXPECT_EQ(r.clientFor(overflow), nullptr);
    EXPECT_EQ(static_cast<int>(r.linkCount()), max);
    EXPECT_EQ(phone.connected, max);  // unchanged — no spurious onConnected
}

TEST(EventRouter, OutOfRangeRoleByteIsDropped)
{
    EventRouter r;
    SpyClient phone;
    r.setRoleClient(LinkRole::Peripheral, &phone);

    // A controller Role byte outside {Central=0, Peripheral=1} must not index
    // past the fixed role table (OOB-read guard) — dropped cleanly.
    EXPECT_FALSE(r.onConnectionComplete(static_cast<LinkRole>(0x02), 0x0050,
            0x01, kAddr, 24, 0, 400));
    EXPECT_EQ(r.clientFor(0x0050), nullptr);
    EXPECT_EQ(r.linkCount(), 0);
    EXPECT_EQ(phone.connected, 0);
}
