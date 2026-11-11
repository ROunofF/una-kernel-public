#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "App/MemAllocator.hpp"
#include "Shared/HeapTelemetry.hpp"

using App::MemAllocator;

namespace App {

// Test seam (declared as a friend by MemAllocator): forges the torn state a
// force-terminate (osThreadTerminate) can leave behind -- a block tracked in
// mBlocks/mSizes whose quota charge and gauge publication were skipped. The
// pointer must come from ::malloc so freeAll() can release it.
struct MemAllocatorTestPeer {
    static void injectUncountedBlock(MemAllocator& a, void* ptr, size_t size)
    {
        a.mBlocks.push_back(ptr);
        a.mSizes[ptr] = size;
    }

    // Model a force-terminate that struck free() mid-retraction. free() frees
    // and erases the block only *after* the gauge stores, so the block stays
    // tracked (freeAll() will still release it); exactly one of the (mirror,
    // global) stores of the pair has landed, and which one depends on the
    // store order under test.
    //
    // Fixed order (mirror-first on subtract): the mirror store lands, the
    // global store does not -> the global is left high relative to the mirror.
    static void tearFreeMirrorFirst(MemAllocator& a, size_t bytes)
    {
        a.mGaugeBytes  -= static_cast<uint32_t>(bytes);
        a.mGaugeBlocks -= 1U;
    }

    // Pre-fix order (global-first on subtract): the global store lands, the
    // mirror store does not -> the mirror is left stale-high above the global.
    // This is the state whose wholesale retraction in freeAll() unsigned-wraps
    // the global; the mirror-first swap is what makes it unreachable.
    static void tearFreeGlobalFirst(size_t bytes)
    {
        HeapTelemetry::AppAllocBytes().fetch_sub(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
        HeapTelemetry::AppAllocBlocks().fetch_sub(1U, std::memory_order_relaxed);
    }
};

} // namespace App

namespace {

// Small quota so the tests exercise the enforcement arithmetic without
// making large host allocations.
constexpr size_t kQuota = 1024;

// The global heap-attribution gauges. Other tests in this binary contribute
// too, so each test captures a baseline and asserts deltas against it.
uint32_t gaugeBytes()  { return HeapTelemetry::AppAllocBytes().load(std::memory_order_relaxed); }
uint32_t gaugeBlocks() { return HeapTelemetry::AppAllocBlocks().load(std::memory_order_relaxed); }

} // namespace

TEST(MemAllocator, UnderQuotaAllocationSucceedsAndIsAccounted)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(a.getOutstandingBytes(), 100U);
    EXPECT_EQ(a.getDenialCount(), 0U);

    void* q = a.malloc(200);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(a.getOutstandingBytes(), 300U);

    a.free(p);
    a.free(q);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
}

TEST(MemAllocator, AllocationExactlyAtQuotaSucceeds)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(kQuota);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(a.getOutstandingBytes(), kQuota);
    EXPECT_EQ(a.getDenialCount(), 0U);

    a.free(p);
}

TEST(MemAllocator, OverQuotaAllocationIsDeniedAndCounted)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(kQuota / 2);
    ASSERT_NE(p, nullptr);

    // Would push outstanding past the quota: denied, nothing charged.
    void* q = a.malloc(kQuota);
    EXPECT_EQ(q, nullptr);
    EXPECT_EQ(a.getOutstandingBytes(), kQuota / 2);
    EXPECT_EQ(a.getDenialCount(), 1U);

    void* r = a.malloc(kQuota);
    EXPECT_EQ(r, nullptr);
    EXPECT_EQ(a.getDenialCount(), 2U);

    a.free(p);
}

TEST(MemAllocator, HugeRequestIsDeniedWithoutOverflow)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(std::numeric_limits<size_t>::max());
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(a.getDenialCount(), 1U);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
}

TEST(MemAllocator, FreeRestoresHeadroom)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(kQuota);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(a.malloc(1), nullptr);

    a.free(p);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);

    void* q = a.malloc(kQuota);
    EXPECT_NE(q, nullptr);
    a.free(q);
}

TEST(MemAllocator, FreeAllResetsAccountingAndDenialCounter)
{
    MemAllocator a("TEST.SRV", kQuota);

    ASSERT_NE(a.malloc(400), nullptr);
    ASSERT_NE(a.malloc(400), nullptr);
    EXPECT_EQ(a.malloc(400), nullptr);   // 800 + 400 > 1024
    EXPECT_EQ(a.getDenialCount(), 1U);

    a.freeAll();
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
    EXPECT_EQ(a.getDenialCount(), 0U);

    // Full quota is available again after freeAll (app restart).
    void* p = a.malloc(kQuota);
    EXPECT_NE(p, nullptr);
    a.free(p);
}

TEST(MemAllocator, ReallocWithinQuotaPreservesContentAndAccounting)
{
    MemAllocator a("TEST.SRV", kQuota);

    char* p = static_cast<char*>(a.malloc(64));
    ASSERT_NE(p, nullptr);
    std::memset(p, 0x5A, 64);

    char* q = static_cast<char*>(a.realloc(p, 128));
    ASSERT_NE(q, nullptr);
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_EQ(q[i], 0x5A);
    }
    EXPECT_EQ(a.getOutstandingBytes(), 128U);
    EXPECT_EQ(a.getDenialCount(), 0U);

    a.free(q);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
}

TEST(MemAllocator, ReallocIsDeniedWhenGrowExceedsQuota)
{
    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(kQuota / 2);
    ASSERT_NE(p, nullptr);

    // Documented conservative behavior: old + new are both charged while the
    // copy is pending, so this grow is denied even though the net result
    // (3/4 quota) would fit. The original block stays valid and tracked.
    void* q = a.realloc(p, (kQuota / 4) * 3);
    EXPECT_EQ(q, nullptr);
    EXPECT_EQ(a.getOutstandingBytes(), kQuota / 2);
    EXPECT_EQ(a.getDenialCount(), 1U);

    a.free(p);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
}

TEST(MemAllocator, ExternalPointerFreeIsIgnoredAndNotUncharged)
{
    MemAllocator a("TEST.SRV", kQuota);
    MemAllocator b("TEST.GUI", kQuota);

    void* p = a.malloc(100);
    ASSERT_NE(p, nullptr);

    // A cross-process free must not free or uncharge the sibling's block.
    b.free(p);
    EXPECT_EQ(a.getOutstandingBytes(), 100U);
    EXPECT_EQ(b.getOutstandingBytes(), 0U);

    a.free(p);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);
}

TEST(MemAllocator, DefaultQuotaIsTheNamedConstant)
{
    MemAllocator a("TEST.SRV");
    EXPECT_EQ(a.getQuotaBytes(), MemAllocator::skHeapQuotaBytes);
    EXPECT_EQ(MemAllocator::skHeapQuotaBytes, 512U * 1024U);
}

TEST(MemAllocator, GaugesTrackMallocAndFreeExactly)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    MemAllocator a("TEST.SRV", kQuota);

    void* p = a.malloc(100);
    ASSERT_NE(p, nullptr);
    void* q = a.malloc(200);
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(gaugeBytes() - baseBytes, 300U);
    EXPECT_EQ(gaugeBlocks() - baseBlocks, 2U);

    a.free(p);
    EXPECT_EQ(gaugeBytes() - baseBytes, 200U);
    EXPECT_EQ(gaugeBlocks() - baseBlocks, 1U);

    a.free(q);
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}

TEST(MemAllocator, FreeAllRetractsOnlyThisInstancesGaugeContribution)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    MemAllocator a("TEST.SRV", kQuota);
    MemAllocator b("TEST.GUI", kQuota);
    ASSERT_NE(a.malloc(100), nullptr);
    ASSERT_NE(b.malloc(50), nullptr);

    // a's retraction must return the gauges to "b only", not zero them.
    a.freeAll();
    EXPECT_EQ(gaugeBytes() - baseBytes, 50U);
    EXPECT_EQ(gaugeBlocks() - baseBlocks, 1U);

    b.freeAll();
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}

TEST(MemAllocator, FreeAllRetractsGaugesExactlyDespiteRacedUncountedBlock)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    MemAllocator a("TEST.SRV", kQuota);
    ASSERT_NE(a.malloc(300), nullptr);

    // Simulate the force-terminate race: a block lands in mSizes but the
    // thread dies before the quota charge and the gauge fetch_adds run.
    void* raced = std::malloc(64);
    ASSERT_NE(raced, nullptr);
    App::MemAllocatorTestPeer::injectUncountedBlock(a, raced, 64);
    EXPECT_EQ(gaugeBytes() - baseBytes, 300U);   // raced block never published
    EXPECT_EQ(gaugeBlocks() - baseBlocks, 1U);

    // freeAll() releases both blocks but must retract only what was
    // published: the gauges return exactly to baseline instead of drifting
    // 64 bytes / 1 block low for the rest of the device's uptime.
    a.freeAll();
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);

    // Quota enforcement and gauge accounting are both intact afterwards.
    void* p = a.malloc(kQuota);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(gaugeBytes() - baseBytes, kQuota);
    a.free(p);
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}

// F1: a force-terminate tearing free()'s retraction must never let freeAll()
// over-retract and unsigned-wrap the global gauge below the other allocators'
// shares. With the mirror-first-on-subtract order, the tear leaves the mirror
// low and the global high, so the subsequent freeAll() drifts the global
// harmlessly high (>= baseline) rather than wrapping it.
TEST(MemAllocator, FreeSideTornWindowDoesNotWrapGlobalGauge)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    // Two allocators so the global also holds a *smaller* sibling share -- the
    // arithmetic under which an over-retraction would wrap below baseline.
    MemAllocator a("TEST.SRV", 4096);
    MemAllocator b("TEST.GUI", 4096);
    void* pa = a.malloc(1024);
    ASSERT_NE(pa, nullptr);
    void* pb = b.malloc(64);
    ASSERT_NE(pb, nullptr);
    (void)pb;
    EXPECT_EQ(gaugeBytes() - baseBytes, 1024U + 64U);

    // Force-terminate strikes a.free(pa) between the mirror store and the
    // global store (fixed order): a's mirror drops, the global keeps pa's
    // 1024, and pa is still tracked.
    App::MemAllocatorTestPeer::tearFreeMirrorFirst(a, 1024);

    // freeAll() releases pa and retracts a's (now-zero) mirror: the global
    // must stay >= baseline (no wrap), biased high by the un-retracted 1024,
    // with b's 64 still counted.
    a.freeAll();
    EXPECT_GE(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBytes() - baseBytes, 1024U + 64U);
    EXPECT_GE(gaugeBlocks(), baseBlocks);
    EXPECT_EQ(a.getOutstandingBytes(), 0U);

    b.freeAll();
    EXPECT_GE(gaugeBytes(), baseBytes);          // never wrapped below baseline
    EXPECT_EQ(gaugeBytes() - baseBytes, 1024U);  // residual high bias only

    // The 1024-byte / 1-block residual is the accepted "persists until reboot"
    // bias (F2); on real hardware it would sit on the global until reset.
    // Clear it here for test isolation so the shared static returns to
    // baseline for siblings.
    HeapTelemetry::AppAllocBytes().fetch_sub(1024U, std::memory_order_relaxed);
    HeapTelemetry::AppAllocBlocks().fetch_sub(1U, std::memory_order_relaxed);
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}

// F1 (counter-case, documents why the swap is needed): the pre-fix
// global-first order leaves the *mirror* stale-high above the global; freeAll
// then over-retracts and the global unsigned-wraps far above baseline. This
// is the catastrophe the mirror-first order makes unreachable.
TEST(MemAllocator, MirrorAheadOfGlobalWrapsFreeAll)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    MemAllocator a("TEST.SRV", 4096);
    MemAllocator b("TEST.GUI", 4096);
    void* pa = a.malloc(1024);
    ASSERT_NE(pa, nullptr);
    void* pb = b.malloc(64);
    ASSERT_NE(pb, nullptr);
    (void)pa;
    (void)pb;

    // Model the pre-fix global-first tear on a.free(pa): the global loses pa's
    // 1024, but a's mirror still claims it (and pa is still tracked).
    App::MemAllocatorTestPeer::tearFreeGlobalFirst(1024);
    EXPECT_EQ(gaugeBytes() - baseBytes, 64U);  // global now holds only b's 64

    // freeAll() retracts a's stale-high mirror (1024) from a global holding
    // only 64 above baseline -> unsigned underflow, i.e. a huge value well
    // above baseline rather than the >= baseline the invariant promises.
    a.freeAll();
    EXPECT_GT(gaugeBytes(), baseBytes + 0x1000000U);

    // This test deliberately corrupted the shared global; restore it exactly
    // so sibling tests are unaffected (b still owns pb, released here).
    b.freeAll();
    HeapTelemetry::AppAllocBytes().fetch_add(baseBytes - gaugeBytes(), std::memory_order_relaxed);
    HeapTelemetry::AppAllocBlocks().fetch_add(baseBlocks - gaugeBlocks(), std::memory_order_relaxed);
    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}

TEST(MemAllocator, DestructorRetractsGaugeContribution)
{
    const uint32_t baseBytes  = gaugeBytes();
    const uint32_t baseBlocks = gaugeBlocks();

    {
        MemAllocator a("TEST.SRV", kQuota);
        ASSERT_NE(a.malloc(128), nullptr);

        void* raced = std::malloc(32);
        ASSERT_NE(raced, nullptr);
        App::MemAllocatorTestPeer::injectUncountedBlock(a, raced, 32);
    }

    EXPECT_EQ(gaugeBytes(), baseBytes);
    EXPECT_EQ(gaugeBlocks(), baseBlocks);
}
