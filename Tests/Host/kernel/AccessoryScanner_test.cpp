/**
 * Host unit tests for Ble::AccessoryScanner -- the single owner of the
 * GAP discovery scan. Exercised against a mock IGapScanOps, following the
 * IGattClientOps pattern, so the scanning logic is testable without a controller.
 *
 * The single-owner property is the point: GAP discovery is one global resource,
 * and with several accessory links racing to acquire, "is the radio already
 * scanning" stops being implicit in one state machine and has to be enforced.
 */

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "Components/External/BLE/Accessory/AccessoryScanner.hpp"

namespace {

using Ble::AccessoryScanner;
using Ble::IGapScanOps;

constexpr uint16_t kHrs = 0x180D;
constexpr uint16_t kCsc = 0x1816;
constexpr uint16_t kCps = 0x1818;

class MockScanOps : public IGapScanOps {
public:
    bool startGeneralDiscovery(uint16_t interval, uint16_t window,
            bool filterDup) override
    {
        ++startCalls;
        lastInterval = interval;
        lastWindow = window;
        lastFilterDup = filterDup;
        return startResult;
    }
    bool terminateGeneralDiscovery() override
    {
        ++stopCalls;
        return stopResult;
    }

    int  startCalls = 0;
    int  stopCalls = 0;
    bool startResult = true;
    bool stopResult = true;
    uint16_t lastInterval = 0;
    uint16_t lastWindow = 0;
    bool lastFilterDup = false;
};

class RecordingSink : public AccessoryScanner::MatchCallback {
public:
    void onScanMatch(const AccessoryScanner::Match &m) override
    {
        matches.push_back(m);
    }
    std::vector<AccessoryScanner::Match> matches;
};

// Build an advertisement listing one 16-bit service UUID plus a local name.
std::vector<uint8_t> advWith(uint16_t uuid, const char *name)
{
    std::vector<uint8_t> v;
    v.push_back(0x03);              // len (type + 2 UUID bytes)
    v.push_back(0x03);              // complete list of 16-bit UUIDs
    v.push_back(static_cast<uint8_t>(uuid & 0xFF));
    v.push_back(static_cast<uint8_t>(uuid >> 8));
    if (name != nullptr) {
        const uint8_t n = static_cast<uint8_t>(std::strlen(name));
        v.push_back(static_cast<uint8_t>(n + 1));
        v.push_back(0x09);          // complete local name
        v.insert(v.end(), name, name + n);
    }
    return v;
}

const uint8_t kAddr[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

} // namespace

TEST(AccessoryScanner, RefusesToScanWithNoWantedUuids)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    RecordingSink sink;

    EXPECT_FALSE(s.start(&sink));
    EXPECT_EQ(ops.startCalls, 0) << "must not burn radio time scanning for nothing";
    EXPECT_FALSE(s.isScanning());
}

TEST(AccessoryScanner, StartsWithDuplicateFilteringOn)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;

    ASSERT_TRUE(s.start(&sink));
    EXPECT_TRUE(s.isScanning());
    EXPECT_EQ(ops.startCalls, 1);
    // Duplicate filtering must stay on: repeat reports otherwise back the
    // controller event queue up into events-lost.
    EXPECT_TRUE(ops.lastFilterDup);
    EXPECT_EQ(ops.lastInterval, 0x00A0);
    EXPECT_EQ(ops.lastWindow, 0x0040);
}

TEST(AccessoryScanner, SecondStartIsRefusedWhileScanning)
{
    // The single-owner guard: two links must not both believe they hold the radio.
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;

    ASSERT_TRUE(s.start(&sink));
    EXPECT_FALSE(s.start(&sink));
    EXPECT_EQ(ops.startCalls, 1) << "must not issue a second discovery procedure";
    EXPECT_TRUE(s.isScanning());
}

TEST(AccessoryScanner, RejectedStartLeavesScannerStopped)
{
    // The caller's back-off ladder needs a clean state to retry from.
    MockScanOps ops;
    ops.startResult = false;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;

    EXPECT_FALSE(s.start(&sink));
    EXPECT_FALSE(s.isScanning());
    // And a later start must be allowed through, not blocked by a stale flag.
    ops.startResult = true;
    EXPECT_TRUE(s.start(&sink));
}

TEST(AccessoryScanner, StopReleasesTheRadioEvenIfControllerRefuses)
{
    // A refused termination leaves the procedure in an unknown state. Claiming
    // to still own the radio would deadlock the caller out of recovery.
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));

    ops.stopResult = false;
    EXPECT_FALSE(s.stop());
    EXPECT_FALSE(s.isScanning()) << "must not stay latched as owner";
    EXPECT_TRUE(s.start(&sink)) << "recovery must be possible";
}

TEST(AccessoryScanner, StopWhenNotScanningIsHarmless)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    EXPECT_FALSE(s.stop());
    EXPECT_EQ(ops.stopCalls, 0);
}

TEST(AccessoryScanner, ReportsMatchWithUuidNameAndAddress)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));

    auto adv = advWith(kCsc, "SPD-BLE0000001");
    EXPECT_TRUE(s.onAdvReport(0x01, kAddr, adv.data(),
            static_cast<uint8_t>(adv.size()), -55));

    ASSERT_EQ(sink.matches.size(), 1u);
    const auto &m = sink.matches[0];
    EXPECT_EQ(m.matchedUuid, kCsc);
    EXPECT_EQ(m.addrType, 0x01);
    EXPECT_EQ(m.rssi, -55);
    EXPECT_STREQ(m.name, "SPD-BLE0000001");
    EXPECT_EQ(std::memcmp(m.addr, kAddr, 6), 0);
}

TEST(AccessoryScanner, DistinguishesWhichKindMatchedInOneBurst)
{
    // The multi-profile case: one burst hunting three services must tell the
    // caller which kind each hit was, so it lands in the right link slot.
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kHrs));
    ASSERT_TRUE(s.addWantedUuid(kCps));
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));

    auto strap = advWith(kHrs, "COROS HR");
    auto speed = advWith(kCsc, "SPD-BLE0000001");
    s.onAdvReport(0x01, kAddr, strap.data(), static_cast<uint8_t>(strap.size()), -60);
    s.onAdvReport(0x01, kAddr, speed.data(), static_cast<uint8_t>(speed.size()), -50);

    ASSERT_EQ(sink.matches.size(), 2u);
    EXPECT_EQ(sink.matches[0].matchedUuid, kHrs);
    EXPECT_EQ(sink.matches[1].matchedUuid, kCsc);
}

TEST(AccessoryScanner, IgnoresNonMatchingAdvertisers)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));

    auto other = advWith(0x1826, "Some Trainer");   // FTMS, not wanted
    EXPECT_FALSE(s.onAdvReport(0x01, kAddr, other.data(),
            static_cast<uint8_t>(other.size()), -50));
    EXPECT_TRUE(sink.matches.empty());
}

TEST(AccessoryScanner, IgnoresReportsWhenNotScanning)
{
    // Reports can arrive after a burst ends; they must not be dispatched.
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));
    ASSERT_TRUE(s.stop());

    auto adv = advWith(kCsc, "SPD-BLE0000001");
    EXPECT_FALSE(s.onAdvReport(0x01, kAddr, adv.data(),
            static_cast<uint8_t>(adv.size()), -50));
    EXPECT_TRUE(sink.matches.empty());
}

TEST(AccessoryScanner, DiscoveryCompleteReleasesOwnership)
{
    // The controller can end the procedure by itself; isScanning() must not lie,
    // or the caller will never initiate a connection.
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    RecordingSink sink;
    ASSERT_TRUE(s.start(&sink));

    s.onDiscoveryComplete();
    EXPECT_FALSE(s.isScanning());
    EXPECT_TRUE(s.start(&sink));
}

TEST(AccessoryScanner, WantedUuidSetIsIdempotentAndBounded)
{
    MockScanOps ops;
    AccessoryScanner s(ops);

    EXPECT_TRUE(s.addWantedUuid(kCsc));
    EXPECT_TRUE(s.addWantedUuid(kCsc));           // duplicate is a no-op
    EXPECT_EQ(s.wantedCount(), 1);

    EXPECT_TRUE(s.addWantedUuid(kHrs));
    EXPECT_TRUE(s.addWantedUuid(kCps));
    EXPECT_TRUE(s.addWantedUuid(0x1826));
    EXPECT_EQ(s.wantedCount(), AccessoryScanner::kMaxWantedUuids);

    EXPECT_FALSE(s.addWantedUuid(0x1234)) << "must refuse rather than overflow";
    EXPECT_EQ(s.wantedCount(), AccessoryScanner::kMaxWantedUuids);

    s.clearWantedUuids();
    EXPECT_EQ(s.wantedCount(), 0);
}

TEST(AccessoryScanner, NullCallbackStillMatchesWithoutDispatching)
{
    MockScanOps ops;
    AccessoryScanner s(ops);
    ASSERT_TRUE(s.addWantedUuid(kCsc));
    ASSERT_TRUE(s.start(nullptr));

    auto adv = advWith(kCsc, "SPD-BLE0000001");
    EXPECT_TRUE(s.onAdvReport(0x01, kAddr, adv.data(),
            static_cast<uint8_t>(adv.size()), -50));
}

// --- Names that arrive only in the scan response -----------------------------
//
// The Polar H10 puts its Local Name in the SCAN_RSP, not the ADV_IND. A scan
// response carries no service UUID, so the wanted-UUID filter discarded it and
// the strap listed as "Unknown". These pin the merge, and pin that acquisition
// is deliberately unaffected.

namespace {

// A scan response carrying only a local name -- no service UUID, which is
// exactly why the plain filter drops it.
std::vector<uint8_t> scanRspWithName(const char *name)
{
    std::vector<uint8_t> v;
    const uint8_t n = static_cast<uint8_t>(std::strlen(name));
    v.push_back(static_cast<uint8_t>(n + 1));
    v.push_back(0x09);              // complete local name
    v.insert(v.end(), name, name + n);
    return v;
}

}  // namespace

TEST(AccessoryScanner, ScanResponseSuppliesTheNameForANamelessAdvertiser)
{
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    // Nameless ADV_IND: held, not reported.
    const auto adv = advWith(0x180D, nullptr);
    EXPECT_TRUE(s.onAdvReport(0x01, kAddr, adv.data(),
            static_cast<uint8_t>(adv.size()), -55,
            AccessoryScanner::kEvtAdvInd));
    EXPECT_TRUE(sink.matches.empty());

    // The scan response completes it.
    const auto rsp = scanRspWithName("Polar H10 ABC123");
    EXPECT_TRUE(s.onAdvReport(0x01, kAddr, rsp.data(),
            static_cast<uint8_t>(rsp.size()), -55,
            AccessoryScanner::kEvtScanRsp));

    ASSERT_EQ(sink.matches.size(), 1u);
    EXPECT_STREQ(sink.matches[0].name, "Polar H10 ABC123");
    EXPECT_EQ(sink.matches[0].matchedUuid, 0x180D);
    EXPECT_EQ(sink.matches[0].rssi, -55);
    EXPECT_EQ(std::memcmp(sink.matches[0].addr, kAddr, 6), 0);
}

TEST(AccessoryScanner, NamedAdvertiserIsReportedImmediatelyNotHeld)
{
    // COROS/Garmin/Coospo name themselves in the ADV_IND, so nothing changes
    // for them: no waiting on a scan response that may never come.
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    const auto adv = advWith(0x180D, "COROS HR 0A0B0C");
    s.onAdvReport(0x01, kAddr, adv.data(), static_cast<uint8_t>(adv.size()),
            -60, AccessoryScanner::kEvtAdvInd);

    ASSERT_EQ(sink.matches.size(), 1u);
    EXPECT_STREQ(sink.matches[0].name, "COROS HR 0A0B0C");
}

TEST(AccessoryScanner, HeldMatchIsStillReportedIfNoScanResponseArrives)
{
    // A peer that never answers a scan request must not vanish from the list;
    // it appears at the end of the burst instead.
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    const auto adv = advWith(0x180D, nullptr);
    s.onAdvReport(0x01, kAddr, adv.data(), static_cast<uint8_t>(adv.size()),
            -70, AccessoryScanner::kEvtAdvInd);
    EXPECT_TRUE(sink.matches.empty());

    s.stop();

    ASSERT_EQ(sink.matches.size(), 1u);
    EXPECT_STREQ(sink.matches[0].name, "");
    EXPECT_EQ(sink.matches[0].matchedUuid, 0x180D);
}

TEST(AccessoryScanner, AcquisitionIsNotDelayedByNameCompletion)
{
    // setAwaitNames defaults OFF: acquisition connects by address, so a
    // nameless strap must be reported at once rather than held for a burst.
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    ASSERT_TRUE(s.start(&sink));

    const auto adv = advWith(0x180D, nullptr);
    s.onAdvReport(0x01, kAddr, adv.data(), static_cast<uint8_t>(adv.size()),
            -55, AccessoryScanner::kEvtAdvInd);

    ASSERT_EQ(sink.matches.size(), 1u);
    EXPECT_STREQ(sink.matches[0].name, "");
}

TEST(AccessoryScanner, ScanResponseFromAnUnmatchedAddressIsIgnored)
{
    // The exemption is only for an address this burst already matched --
    // otherwise any passing device's scan response would enter the list.
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    const auto rsp = scanRspWithName("Someone Elses Phone");
    EXPECT_FALSE(s.onAdvReport(0x01, kAddr, rsp.data(),
            static_cast<uint8_t>(rsp.size()), -40,
            AccessoryScanner::kEvtScanRsp));
    EXPECT_TRUE(sink.matches.empty());
}

TEST(AccessoryScanner, ANamelessScanResponseKeepsTheMatchHeld)
{
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    const auto adv = advWith(0x180D, nullptr);
    s.onAdvReport(0x01, kAddr, adv.data(), static_cast<uint8_t>(adv.size()),
            -55, AccessoryScanner::kEvtAdvInd);

    const std::vector<uint8_t> emptyRsp { 0x01, 0x09 };   // name AD, zero chars
    s.onAdvReport(0x01, kAddr, emptyRsp.data(),
            static_cast<uint8_t>(emptyRsp.size()), -55,
            AccessoryScanner::kEvtScanRsp);
    EXPECT_TRUE(sink.matches.empty());

    s.stop();
    EXPECT_EQ(sink.matches.size(), 1u);
}

TEST(AccessoryScanner, HeldMatchesDoNotLeakAcrossBursts)
{
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);

    ASSERT_TRUE(s.start(&sink));
    const auto adv = advWith(0x180D, nullptr);
    s.onAdvReport(0x01, kAddr, adv.data(), static_cast<uint8_t>(adv.size()),
            -55, AccessoryScanner::kEvtAdvInd);
    s.stop();
    ASSERT_EQ(sink.matches.size(), 1u);

    // A fresh burst starts with nothing held, so stopping it reports nothing.
    ASSERT_TRUE(s.start(&sink));
    s.stop();
    EXPECT_EQ(sink.matches.size(), 1u);
}

TEST(AccessoryScanner, MoreNamelessAdvertisersThanSlotsAreStillReported)
{
    // Overflow must degrade to the pre-existing behaviour (report at once),
    // never to dropping a device.
    MockScanOps ops;
    RecordingSink sink;
    AccessoryScanner s(ops);
    s.addWantedUuid(0x180D);
    s.setAwaitNames(true);
    ASSERT_TRUE(s.start(&sink));

    const auto adv = advWith(0x180D, nullptr);
    uint8_t addr[6] = { 0, 0, 0, 0, 0, 0 };
    for (uint8_t i = 0; i < 12; ++i) {
        addr[0] = i;
        s.onAdvReport(0x01, addr, adv.data(),
                static_cast<uint8_t>(adv.size()), -55,
                AccessoryScanner::kEvtAdvInd);
    }
    s.stop();

    EXPECT_EQ(sink.matches.size(), 12u);
}
