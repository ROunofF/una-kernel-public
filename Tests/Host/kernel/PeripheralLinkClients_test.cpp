// Host unit tests for the serialised GATT-client discovery coordinator.

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "Components/External/BLE/Discovery/PeripheralLinkClients.hpp"
#include "Stub/BleServerStub.hpp"

using namespace Ble;

namespace {

// ATT "Attribute Not Found": marks the end of a discovery stage.
constexpr uint8_t kAttErrNotFound = 0x0A;
constexpr uint16_t kConn = 0x0040;

// Records the outbound GATT operations the coordinator issues.
class MockOps : public IGattClientOps {
public:
    enum Type { Service, Chars, Descriptors, ValueWrite, DescWrite };
    struct Call {
        Type type;
        uint16_t handle;
    };

    std::vector<Call> calls;
    Ble::UUID lastServiceUuid;
    bool ret { true };

    bool discoverPrimaryServiceByUuid(uint16_t, const Ble::UUID& uuid) override
    {
        lastServiceUuid = uuid;
        calls.push_back({ Service, 0 });
        return ret;
    }
    bool discoverAllChars(uint16_t, uint16_t startHandle, uint16_t) override
    {
        calls.push_back({ Chars, startHandle });
        return ret;
    }
    bool discoverAllCharDescriptors(uint16_t, uint16_t startHandle,
            uint16_t) override
    {
        calls.push_back({ Descriptors, startHandle });
        return ret;
    }
    bool writeCharValue(uint16_t, uint16_t valueHandle, const uint8_t*,
            uint16_t) override
    {
        calls.push_back({ ValueWrite, valueHandle });
        return ret;
    }
    bool writeCharDescriptor(uint16_t, uint16_t descHandle, const uint8_t*,
            uint16_t) override
    {
        calls.push_back({ DescWrite, descHandle });
        return ret;
    }

    int count(Type t) const
    {
        int n = 0;
        for (const Call& c : calls) {
            if (c.type == t) {
                n++;
            }
        }
        return n;
    }
};

// Reports link activity; the test toggles whether the link is "quiet". The
// quiet query now lives on IBle, so back it with the full IBle stub.
class FakeLinkActivity : public Stub::BleServer {
public:
    bool quiet { true };
    bool isTransferQuiet(uint32_t) const override { return quiet; }
};

// Records arm requests; the test fires onTick() on the coordinator itself.
class FakeScheduler : public IGattScheduler {
public:
    int armCount { 0 };
    int cancelCount { 0 };
    uint32_t lastMs { 0 };

    void arm(uint32_t ms) override
    {
        armCount++;
        lastMs = ms;
    }

    void cancel() override
    {
        cancelCount++;
    }
};

// A spy service client with a configurable service and characteristic set.
class SpyClient : public IGattClient {
public:
    SpyClient(PeripheralLinkClients& mgr, const char* svc,
            const std::vector<std::string>& chars)
        : mMgr(mgr)
    {
        mSvc.fromString(svc);
        for (const std::string& s : chars) {
            Ble::UUID u;
            u.fromString(s.c_str());
            mChars.push_back(u);
        }
    }

    bool autoReady { true };

    int resolved { 0 };
    int unavailable { 0 };
    int notifications { 0 };
    int linkDown { 0 };
    int linkReady { 0 };
    uint16_t lastNotifHandle { 0 };
    ResolvedService lastResolved {};

    const Ble::UUID& serviceUuid() const override { return mSvc; }
    uint8_t charCount() const override
    {
        return static_cast<uint8_t>(mChars.size());
    }
    const Ble::UUID& charUuid(uint8_t index) const override
    {
        return mChars[index];
    }

    void onServicesResolved(const ResolvedService& res) override
    {
        resolved++;
        lastResolved = res;
        if (autoReady) {
            mMgr.clientReady();
        }
    }
    void onServiceUnavailable() override { unavailable++; }
    void onNotification(uint16_t attrHandle, const uint8_t*, uint8_t) override
    {
        notifications++;
        lastNotifHandle = attrHandle;
    }
    void onLinkDown() override { linkDown++; }
    void onLinkReady() override { linkReady++; }

    void triggerReady() { mMgr.clientReady(); }

private:
    PeripheralLinkClients& mMgr;
    Ble::UUID mSvc;
    std::vector<Ble::UUID> mChars;
};

// Builds a 128-bit handle/value pair as delivered by read-by-type.
std::array<uint8_t, 21> makeCharPair(const Ble::UUID& uuid, uint16_t declHandle,
        uint8_t props, uint16_t valueHandle)
{
    std::array<uint8_t, 21> b {};
    memcpy(&b[0], &declHandle, 2);
    b[2] = props;
    memcpy(&b[3], &valueHandle, 2);
    memcpy(&b[5], uuid.get128(), 16);
    return b;
}

// Drives a client through a successful service+char+descriptor discovery
// (without the trailing readiness handshake, which the caller controls).
void discoverClient(PeripheralLinkClients& mgr, const Ble::UUID& charUuid,
        uint16_t srvStart, uint16_t srvEnd, uint16_t declHandle,
        uint16_t valueHandle, uint16_t cccdHandle)
{
    mgr.onTick();                                   // issue service discovery
    mgr.onServiceGroupFound(srvStart, srvEnd);
    mgr.onGattError(0, 0, kAttErrNotFound);         // -> char discovery stage
    mgr.onTick();                                   // issue char discovery
    auto cp = makeCharPair(charUuid, declHandle, 0x10, valueHandle);
    mgr.onCharDiscovered(cp.data(), static_cast<uint8_t>(cp.size()));
    mgr.onGattError(0, 0, kAttErrNotFound);         // -> descriptor discovery
    mgr.onTick();                                   // issue descriptor discovery
    mgr.onCharDescFound(cccdHandle, 0x2902);        // CCCD for the characteristic
    mgr.onGattProcComplete(0);                      // -> resolve
}

const char* kSvcA  = "11111111-1111-1111-1111-111111111111";
const char* kCharA = "22222222-2222-2222-2222-222222222222";
const char* kSvcB  = "33333333-3333-3333-3333-333333333333";
const char* kCharB = "44444444-4444-4444-4444-444444444444";

} // namespace

TEST(PeripheralLinkClients, SerialisesDiscoveryAcrossClients)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    a.autoReady = false;  // hold the handshake to observe serialisation
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);
    EXPECT_EQ(sched.armCount, 1);  // settle delay armed

    // CCCD discovered at value+2 (not the value+1 fallback) must be honoured.
    discoverClient(mgr, a.charUuid(0), 0x0010, 0x0020, 0x0011, 0x0012, 0x0014);

    EXPECT_EQ(a.resolved, 1);
    ASSERT_EQ(a.lastResolved.charCount, 1);
    EXPECT_EQ(a.lastResolved.chars[0].valueHandle, 0x0012);
    EXPECT_EQ(a.lastResolved.chars[0].cccdHandle, 0x0014);
    EXPECT_TRUE(a.lastResolved.chars[0].cccdFromDescriptor);  // found by discovery

    // Client B must not have started discovery while A is not ready.
    EXPECT_EQ(ops.count(MockOps::Service), 1);

    // A finishes its setup: the coordinator advances to B.
    a.triggerReady();
    mgr.onTick();
    EXPECT_EQ(ops.count(MockOps::Service), 2);
    EXPECT_EQ(ops.lastServiceUuid, b.serviceUuid());

    mgr.onServiceGroupFound(0x0030, 0x0040);
    mgr.onGattError(0, 0, kAttErrNotFound);
    mgr.onTick();
    auto cpb = makeCharPair(b.charUuid(0), 0x0031, 0x10, 0x0032);
    mgr.onCharDiscovered(cpb.data(), static_cast<uint8_t>(cpb.size()));
    mgr.onGattError(0, 0, kAttErrNotFound);  // -> descriptor discovery
    mgr.onTick();
    mgr.onCharDescFound(0x0034, 0x2902);     // CCCD at value+2
    mgr.onGattProcComplete(0);               // -> resolve

    EXPECT_EQ(b.resolved, 1);
    EXPECT_EQ(b.lastResolved.chars[0].valueHandle, 0x0032);
    EXPECT_EQ(b.lastResolved.chars[0].cccdHandle, 0x0034);
}

TEST(PeripheralLinkClients, RoutesNotificationsByHandle)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);
    discoverClient(mgr, a.charUuid(0), 0x0010, 0x0020, 0x0011, 0x0012, 0x0014);
    mgr.onTick();  // A ready (auto) -> B discovery
    discoverClient(mgr, b.charUuid(0), 0x0030, 0x0040, 0x0031, 0x0032, 0x0034);

    const uint8_t payload[] = { 0xAA };
    mgr.onNotification(0x0012, payload, sizeof(payload));
    mgr.onNotification(0x0032, payload, sizeof(payload));
    mgr.onNotification(0x0099, payload, sizeof(payload));  // unknown handle

    EXPECT_EQ(a.notifications, 1);
    EXPECT_EQ(a.lastNotifHandle, 0x0012);
    EXPECT_EQ(b.notifications, 1);
    EXPECT_EQ(b.lastNotifHandle, 0x0032);
}

TEST(PeripheralLinkClients, ReadyBroadcastOnlyWhenAllResolved)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);
    discoverClient(mgr, a.charUuid(0), 0x0010, 0x0020, 0x0011, 0x0012, 0x0014);

    // A is resolved and ready, but B is still pending: the link is not ready and
    // no client has been told it is.
    EXPECT_FALSE(mgr.isReady());
    EXPECT_EQ(a.linkReady, 0);
    EXPECT_EQ(b.linkReady, 0);

    mgr.onTick();  // advance to B
    discoverClient(mgr, b.charUuid(0), 0x0030, 0x0040, 0x0031, 0x0032, 0x0034);

    // Every client resolved: each is told the link is ready exactly once.
    EXPECT_TRUE(mgr.isReady());
    EXPECT_EQ(a.linkReady, 1);
    EXPECT_EQ(b.linkReady, 1);
}

TEST(PeripheralLinkClients, StageWatchdogAdvancesWhenCompletionMissing)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);
    mgr.onTick();  // issue service discovery
    EXPECT_EQ(ops.count(MockOps::Service), 1);

    // Service found, but the stage-completion event (0x0A / proc-complete) never
    // arrives (lost under link traffic). The watchdog tick must advance.
    mgr.onServiceGroupFound(0x0010, 0x0020);
    mgr.onTick();  // watchdog -> char stage armed
    mgr.onTick();  // issue char discovery
    EXPECT_EQ(ops.count(MockOps::Chars), 1);

    // Characteristic found, completion event again missing.
    auto cp = makeCharPair(a.charUuid(0), 0x0011, 0x10, 0x0012);
    mgr.onCharDiscovered(cp.data(), static_cast<uint8_t>(cp.size()));
    mgr.onTick();  // watchdog -> descriptor stage armed
    mgr.onTick();  // issue descriptor discovery
    EXPECT_EQ(ops.count(MockOps::Descriptors), 1);

    // Descriptor proc-complete also missing: watchdog resolves with the value+1
    // fallback CCCD rather than hanging.
    mgr.onTick();  // watchdog -> resolveCurrent
    EXPECT_EQ(a.resolved, 1);
    ASSERT_EQ(a.lastResolved.charCount, 1);
    EXPECT_EQ(a.lastResolved.chars[0].cccdHandle, 0x0013);
}

TEST(PeripheralLinkClients, AdvancesOnProcCompleteWhenNoAttError)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    // Discovery driven entirely by procedure-complete (the ATT 0x0A is never
    // delivered, e.g. lost under link traffic) must still advance every stage.
    mgr.start(kConn);
    mgr.onTick();  // issue service discovery
    mgr.onServiceGroupFound(0x0010, 0x0020);
    mgr.onGattProcComplete(0);  // service stage complete (no 0x0A)
    mgr.onTick();               // issue char discovery
    EXPECT_EQ(ops.count(MockOps::Chars), 1);

    auto cp = makeCharPair(a.charUuid(0), 0x0011, 0x10, 0x0012);
    mgr.onCharDiscovered(cp.data(), static_cast<uint8_t>(cp.size()));
    mgr.onGattProcComplete(0);  // char stage complete (no 0x0A)
    mgr.onTick();               // issue descriptor discovery
    EXPECT_EQ(ops.count(MockOps::Descriptors), 1);

    mgr.onCharDescFound(0x0013, 0x2902);
    mgr.onGattProcComplete(0);  // descriptor stage complete
    EXPECT_EQ(a.resolved, 1);
    ASSERT_EQ(a.lastResolved.charCount, 1);
    EXPECT_EQ(a.lastResolved.chars[0].cccdHandle, 0x0013);
}

TEST(PeripheralLinkClients, ServiceNotFoundSkipsToNextClient)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);
    mgr.onTick();                            // A service discovery issued
    mgr.onGattError(0, 0, kAttErrNotFound);  // service not found -> skip A

    EXPECT_EQ(a.unavailable, 1);
    EXPECT_EQ(a.resolved, 0);

    mgr.onTick();                            // B service discovery issued
    EXPECT_EQ(ops.count(MockOps::Service), 2);
    EXPECT_EQ(ops.lastServiceUuid, b.serviceUuid());
}

TEST(PeripheralLinkClients, MissingRequestedCharSkipsClient)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);
    mgr.onTick();
    mgr.onServiceGroupFound(0x0010, 0x0020);
    mgr.onGattError(0, 0, kAttErrNotFound);  // -> char discovery
    mgr.onTick();
    // A different characteristic UUID than the one A requested.
    Ble::UUID other;
    other.fromString("99999999-9999-9999-9999-999999999999");
    auto cp = makeCharPair(other, 0x0011, 0x10, 0x0012);
    mgr.onCharDiscovered(cp.data(), static_cast<uint8_t>(cp.size()));
    mgr.onGattError(0, 0, kAttErrNotFound);  // no match -> unavailable

    EXPECT_EQ(a.resolved, 0);
    EXPECT_EQ(a.unavailable, 1);
}

TEST(PeripheralLinkClients, DiscoveryWaitsForQuietLink)
{
    MockOps ops;
    FakeScheduler sched;
    FakeLinkActivity activity;
    PeripheralLinkClients mgr(ops, sched);
    mgr.setLinkActivity(&activity);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);

    activity.quiet = false;   // a companion app is busy on the link
    mgr.onTick();             // discovery deferred, not issued
    EXPECT_EQ(ops.count(MockOps::Service), 0);

    mgr.onTick();             // still busy -> still deferred
    EXPECT_EQ(ops.count(MockOps::Service), 0);

    activity.quiet = true;    // link settles
    mgr.onTick();             // now discovery proceeds
    EXPECT_EQ(ops.count(MockOps::Service), 1);
}

TEST(PeripheralLinkClients, DiscoveryRequestRetriesThenSucceeds)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);

    ops.ret = false;          // controller momentarily rejects commands
    mgr.onTick();             // service discovery attempt -> rejected -> retry
    EXPECT_EQ(ops.count(MockOps::Service), 1);
    EXPECT_EQ(a.unavailable, 0);  // not given up

    ops.ret = true;           // controller recovers
    mgr.onTick();             // retry succeeds
    EXPECT_EQ(ops.count(MockOps::Service), 2);

    mgr.onServiceGroupFound(0x0010, 0x0020);  // proceeds normally
    EXPECT_EQ(a.unavailable, 0);
}

TEST(PeripheralLinkClients, DiscoveryRequestGivesUpAfterRetries)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);

    ops.ret = false;  // controller keeps rejecting
    // First attempt + kMaxDiscRetry retries, then give up on the next tick.
    for (int i = 0; i < 8; i++) {
        mgr.onTick();
    }
    EXPECT_EQ(a.unavailable, 1);

    // Coordinator advances to the next client despite the failure.
    ops.ret = true;
    mgr.onTick();
    EXPECT_EQ(ops.lastServiceUuid, b.serviceUuid());
}

TEST(PeripheralLinkClients, FallbackCccdWhenNoDescriptorFound)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);
    mgr.onTick();
    mgr.onServiceGroupFound(0x0010, 0x0020);
    mgr.onGattError(0, 0, kAttErrNotFound);  // -> char discovery
    mgr.onTick();
    auto cp = makeCharPair(a.charUuid(0), 0x0011, 0x10, 0x0012);
    mgr.onCharDiscovered(cp.data(), static_cast<uint8_t>(cp.size()));
    mgr.onGattError(0, 0, kAttErrNotFound);  // -> descriptor discovery
    mgr.onTick();
    // No CCCD descriptor reported.
    mgr.onGattProcComplete(0);               // -> resolve with fallback

    ASSERT_EQ(a.resolved, 1);
    EXPECT_EQ(a.lastResolved.chars[0].cccdHandle, 0x0013);  // value + 1 fallback
    EXPECT_FALSE(a.lastResolved.chars[0].cccdFromDescriptor);  // fallback, not real
}

TEST(PeripheralLinkClients, ResetReportsLinkDownAndStopsRouting)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    mgr.registerClient(&a);

    mgr.start(kConn);
    discoverClient(mgr, a.charUuid(0), 0x0010, 0x0020, 0x0011, 0x0012, 0x0014);
    EXPECT_EQ(a.resolved, 1);

    mgr.reset();
    EXPECT_EQ(a.linkDown, 1);
    EXPECT_EQ(sched.cancelCount, 1);  // any pending discovery tick cancelled

    const uint8_t payload[] = { 0xAA };
    mgr.onNotification(0x0012, payload, sizeof(payload));
    EXPECT_EQ(a.notifications, 0);  // routing table cleared by reset
}

TEST(PeripheralLinkClients, ClientReadyWatchdogSkipsStalledClient)
{
    MockOps ops;
    FakeScheduler sched;
    PeripheralLinkClients mgr(ops, sched);

    SpyClient a(mgr, kSvcA, { kCharA });
    a.autoReady = false;  // A resolves but its setup stalls (never reports ready)
    SpyClient b(mgr, kSvcB, { kCharB });
    mgr.registerClient(&a);
    mgr.registerClient(&b);

    mgr.start(kConn);
    discoverClient(mgr, a.charUuid(0), 0x0010, 0x0020, 0x0011, 0x0012, 0x0014);
    ASSERT_EQ(a.resolved, 1);

    // A is resolved but never calls clientReady(): B must not have started.
    EXPECT_EQ(ops.count(MockOps::Service), 1);

    // The client-ready watchdog fires: the stalled client is skipped (not marked
    // unavailable - it did resolve) and the coordinator advances to B.
    mgr.onTick();                    // watchdog -> skip A, begin B
    EXPECT_EQ(a.unavailable, 0);
    mgr.onTick();                    // issue B service discovery
    EXPECT_EQ(ops.count(MockOps::Service), 2);
    EXPECT_EQ(ops.lastServiceUuid, b.serviceUuid());
}
