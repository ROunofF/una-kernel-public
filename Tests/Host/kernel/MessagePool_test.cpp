/**
 * @file MessagePool_test.cpp
 * @brief Ownership claims on pool blocks, and the sweep that reclaims them.
 *
 * The behaviour under test is what lets the kernel take back blocks from a
 * process that has gone away without ever returning them. Two properties carry
 * the whole design and are what these tests are really about:
 *
 *   1. A claim means "this PID owes a reference", so the sweep must hand back a
 *      block once per claim and never touch a reference belonging to anybody
 *      else.
 *   2. Two processes can owe the same block at once -- the one that allocated
 *      it and the one that received it -- so a claim must never displace
 *      another. An earlier single-slot version silently lost the allocator's
 *      claim on every send, which reintroduced the leak it was written to fix.
 *
 * The host cmsis_os2.h stub is a real block allocator laid out the way the
 * target one is (mp_mem + index * blockSize), so the address-to-index
 * arithmetic is genuinely exercised here rather than merely compiled.
 */

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "App/MessagePool.hpp"

namespace {

using App::MessagePool;
using App::POOL_CONFIGS;
using App::POOL_COUNT;

constexpr uint32_t kPidA = 0x1000'0000u;
constexpr uint32_t kPidB = 0x1000'0001u;
constexpr uint32_t kPidC = 0x2000'0000u;

/// ~35 KB of storage, so the pool lives on the heap rather than the stack.
class MessagePoolTest : public ::testing::Test {
protected:
    void SetUp() override { mPool = new MessagePool(); }
    void TearDown() override { delete mPool; }

    /// Allocate one block from the pool that serves @p size.
    void* alloc(size_t size)
    {
        uint8_t poolId = 0xFF;
        void* p = mPool->allocate(size, poolId, 0);
        EXPECT_NE(p, nullptr);
        return p;
    }

    /// Everything the sweep hands back for @p pid, in order.
    std::vector<void*> sweep(uint32_t pid)
    {
        std::vector<void*> taken;
        size_t cursor = 0;
        for (;;) {
            void* p = mPool->takeOwned(pid, cursor);
            if (p == nullptr) {
                break;
            }
            taken.push_back(p);
        }
        return taken;
    }

    MessagePool* mPool = nullptr;
};

// --- the basic contract -----------------------------------------------------

TEST_F(MessagePoolTest, SweepReturnsAClaimedBlock)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_EQ(sweep(kPidA), std::vector<void*>{b});
}

TEST_F(MessagePoolTest, SweepIgnoresUnclaimedBlocks)
{
    // A message sitting in a queue is unclaimed: the reference is the sender's
    // until the receiver pops it. The sweep must not touch it, or it would free
    // the block out from under the queue.
    void* b = alloc(32);
    (void)b;

    EXPECT_TRUE(sweep(kPidA).empty());
}

TEST_F(MessagePoolTest, SweepReturnsEachBlockOnlyOnce)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_EQ(sweep(kPidA).size(), 1u);
    // The claim is consumed as the block is taken; a second pass finds nothing.
    // Without that the caller's loop would never terminate, because dropping
    // one reference need not free a refcounted message.
    EXPECT_TRUE(sweep(kPidA).empty());
}

TEST_F(MessagePoolTest, SweepDoesNotTakeAnotherProcessesClaim)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_TRUE(sweep(kPidB).empty());
    EXPECT_EQ(sweep(kPidA).size(), 1u);
}

// --- two owners, which is why there are two slots ---------------------------

TEST_F(MessagePoolTest, AllocatorAndReceiverCanBothOweTheSameBlock)
{
    // The case a single slot got wrong: the service allocates a message (and
    // still owes its own reference after sending it), the GUI receives it and
    // owes the one it was handed. Both must be reclaimable.
    void* b = alloc(64);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidB));

    EXPECT_EQ(sweep(kPidA), std::vector<void*>{b});
    EXPECT_EQ(sweep(kPidB), std::vector<void*>{b});
}

TEST_F(MessagePoolTest, ASecondClaimDoesNotDisplaceTheFirst)
{
    void* b = alloc(64);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidB));

    // Sweeping B must leave A's claim intact -- the bug being guarded against
    // is B's claim having overwritten A's on the way in.
    ASSERT_EQ(sweep(kPidB).size(), 1u);
    EXPECT_EQ(sweep(kPidA).size(), 1u);
}

TEST_F(MessagePoolTest, OneProcessCanOweTheSameBlockTwice)
{
    // Claims count references, they do not identify processes. A process that
    // allocates a message and later receives that same message back from its
    // peer owes two references, and both must come back -- collapsing them
    // onto one claim would leave the second unreclaimable, which is the leak
    // this whole mechanism exists to close, and it would be silent.
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_EQ(sweep(kPidA).size(), 2u);
    EXPECT_TRUE(sweep(kPidA).empty());
}

TEST_F(MessagePoolTest, GivingOneOfTwoClaimsBackLeavesTheOther)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_TRUE(mPool->removeOwner(b, kPidA));

    // Still owes one.
    EXPECT_EQ(sweep(kPidA).size(), 1u);
}

TEST_F(MessagePoolTest, AThirdClaimIsRefusedRatherThanDisplacingAnother)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidB));

    // Degrades to "not reclaimable for C", which is a leak. Displacing A or B
    // would be a use-after-free, which is worse.
    EXPECT_FALSE(mPool->addOwner(b, kPidC));

    EXPECT_EQ(sweep(kPidA).size(), 1u);
    EXPECT_EQ(sweep(kPidB).size(), 1u);
    EXPECT_TRUE(sweep(kPidC).empty());
}

// --- giving a claim back ----------------------------------------------------

TEST_F(MessagePoolTest, RemoveOwnerStopsTheSweepFindingIt)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    EXPECT_TRUE(mPool->removeOwner(b, kPidA));

    EXPECT_TRUE(sweep(kPidA).empty());
}

TEST_F(MessagePoolTest, RemoveOwnerOnlyClearsYourOwnClaim)
{
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));

    EXPECT_FALSE(mPool->removeOwner(b, kPidB));
    EXPECT_EQ(sweep(kPidA).size(), 1u);
}

TEST_F(MessagePoolTest, RemoveOwnerLeavesTheOtherSlotAlone)
{
    void* b = alloc(64);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidB));

    EXPECT_TRUE(mPool->removeOwner(b, kPidA));

    EXPECT_TRUE(sweep(kPidA).empty());
    EXPECT_EQ(sweep(kPidB).size(), 1u);
}

TEST_F(MessagePoolTest, DeallocateDropsEveryClaim)
{
    // A block on the free list must carry no claim, or the next sweep would
    // hand out free memory and the destructor would dispatch through the
    // free-list link.
    void* b = alloc(64);
    ASSERT_TRUE(mPool->addOwner(b, kPidA));
    ASSERT_TRUE(mPool->addOwner(b, kPidB));

    mPool->deallocate(b);

    EXPECT_TRUE(sweep(kPidA).empty());
    EXPECT_TRUE(sweep(kPidB).empty());
}

// --- the sentinel -----------------------------------------------------------

TEST_F(MessagePoolTest, TheNoOwnerSentinelIsNeverAClaim)
{
    void* b = alloc(32);

    EXPECT_FALSE(mPool->addOwner(b, MessagePool::skNoOwner));
    EXPECT_TRUE(sweep(MessagePool::skNoOwner).empty());
}

TEST_F(MessagePoolTest, PidZeroIsSweptLikeAnyOther)
{
    // 0 is a legal PID -- they derive from (pathCRC & 0xFFFFFFF0) -- which is
    // why the sentinel is 0xFFFFFFFF and not 0. An app whose CRC lands below
    // 0x10 must not be silently skipped.
    void* b = alloc(32);
    ASSERT_TRUE(mPool->addOwner(b, 0u));

    EXPECT_EQ(sweep(0u), std::vector<void*>{b});
}

// --- address-to-index arithmetic across all four pools ----------------------

TEST_F(MessagePoolTest, EveryBlockOfEveryPoolIsClaimableAndDistinct)
{
    // Exhausts all four pools and claims every block. Catches an index map that
    // aliases two blocks onto one entry, or that fails to cover a pool: with
    // 300 blocks and one claim each, the sweep must return exactly 300 distinct
    // addresses.
    std::vector<void*> all;
    for (size_t p = 0; p < POOL_COUNT; ++p) {
        for (size_t i = 0; i < POOL_CONFIGS[p].blockCount; ++i) {
            uint8_t poolId = 0xFF;
            void* b = mPool->allocate(POOL_CONFIGS[p].blockSize, poolId, 0);
            ASSERT_NE(b, nullptr) << "pool " << p << " block " << i;
            ASSERT_EQ(poolId, p);
            ASSERT_TRUE(mPool->addOwner(b, kPidA));
            all.push_back(b);
        }
    }

    const std::set<void*> unique(all.begin(), all.end());
    EXPECT_EQ(unique.size(), all.size()) << "pool handed out a duplicate block";

    const std::vector<void*> taken = sweep(kPidA);
    EXPECT_EQ(taken.size(), all.size());
    EXPECT_EQ(std::set<void*>(taken.begin(), taken.end()), unique);
}

TEST_F(MessagePoolTest, ClaimsOnDifferentPoolsDoNotAlias)
{
    // One block from each pool, each with a different owner. If two pools
    // mapped onto overlapping index ranges, a sweep of one PID would pick up
    // another's block.
    std::vector<void*> blocks;
    for (size_t p = 0; p < POOL_COUNT; ++p) {
        void* b = alloc(POOL_CONFIGS[p].blockSize);
        ASSERT_TRUE(mPool->addOwner(b, static_cast<uint32_t>(0x3000'0000u + p)));
        blocks.push_back(b);
    }

    for (size_t p = 0; p < POOL_COUNT; ++p) {
        const std::vector<void*> taken = sweep(static_cast<uint32_t>(0x3000'0000u + p));
        ASSERT_EQ(taken.size(), 1u) << "pool " << p;
        EXPECT_EQ(taken[0], blocks[p]) << "pool " << p;
    }
}

TEST_F(MessagePoolTest, AnAddressOutsideThePoolIsRejected)
{
    int notOurs = 0;

    EXPECT_FALSE(mPool->addOwner(&notOurs, kPidA));
    EXPECT_FALSE(mPool->removeOwner(&notOurs, kPidA));
    EXPECT_TRUE(sweep(kPidA).empty());
}

// --- reuse ------------------------------------------------------------------

TEST_F(MessagePoolTest, AReusedBlockStartsWithNoClaims)
{
    void* first = alloc(32);
    ASSERT_TRUE(mPool->addOwner(first, kPidA));
    mPool->deallocate(first);

    void* second = alloc(32);
    ASSERT_EQ(second, first) << "stub should hand the same block straight back";

    // Inheriting the previous tenant's claim would let a sweep of a long-gone
    // process free a block that is now in use by somebody else.
    EXPECT_TRUE(sweep(kPidA).empty());
}

} // namespace
