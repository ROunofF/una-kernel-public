/**
 * @file MessageManagerSweep_test.cpp
 * @brief MessageManager::releaseByOwner -- the sweep that reclaims blocks from
 *        a process that has gone away.
 *
 * MessagePool_test.cpp covers the claim table underneath. This covers the layer
 * that decides what to DO with a claimed block, which is where the two
 * irreversible actions live: dropping a reference, and returning a block to the
 * pool behind the back of the reference count and the destructor.
 */

#include <gtest/gtest.h>

#include <cstring>

#include "App/MessageManager.hpp"
#include "SDK/Messages/MessageBase.hpp"

namespace {

using App::MessageManager;
using App::MessagePool;

constexpr uint32_t kPidA = 0x1000'0000u;
constexpr uint32_t kPidB = 0x1000'0001u;

/// A message of the smallest pooled size, constructed the way the real ones
/// are: the base sets the reference count to 1, which is what distinguishes a
/// constructed message from a block that was only ever allocated.
struct TestMessage : public SDK::MessageBase {
    TestMessage() : SDK::MessageBase(SDK::MessageType::EVENT_BUTTON) {}
};

/// Declares a destructor, which no real message type does and none may. It is
/// here to pin the one thing about the release path that is otherwise invisible:
/// the destructor call is NOT virtual.
///
/// That matters because a message an app allocates is placement-new'd inside
/// the app, so its vptr points into the app image -- which Loader::unload()
/// frees. Dispatching through it destroys a message after its app is gone, and
/// the surviving half of an app releasing a message its dead peer allocated
/// hits the same thing. A virtual call there jumps into freed heap.
struct DestructorWatchMessage : public SDK::MessageBase {
    static bool sDestroyed;
    DestructorWatchMessage() : SDK::MessageBase(SDK::MessageType::EVENT_BUTTON) {}
    ~DestructorWatchMessage() override { sDestroyed = true; }
};
bool DestructorWatchMessage::sDestroyed = false;

class SweepTest : public ::testing::Test {
protected:
    void SetUp() override { mMgr = new MessageManager(); }
    void TearDown() override { delete mMgr; }

    size_t freeBlocks(uint8_t poolId) const
    {
        return mMgr->getPool().getFreeCount(poolId);
    }

    /// Whichever pool serves TestMessage. Derived rather than hardcoded: the
    /// host has 64-bit pointers, so MessageBase is wider here than on target
    /// and lands in a different pool. The sweep logic under test does not care
    /// which, but the free-count assertions have to look at the right one.
    uint8_t pool() const
    {
        return mMgr->getPool().findPoolForSize(sizeof(TestMessage));
    }

    MessageManager* mMgr = nullptr;
};

TEST_F(SweepTest, ReleasingDoesNotDispatchThroughTheMessagesVtable)
{
    // If this fails, releaseMessage has gone back to an unqualified
    // msg->~MessageBase(), and every message an unloaded app allocated is now
    // destroyed through a vtable in freed memory.
    DestructorWatchMessage::sDestroyed = false;

    auto* msg = mMgr->allocateMessage<DestructorWatchMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->releaseMessage(msg);

    EXPECT_FALSE(DestructorWatchMessage::sDestroyed)
            << "the destructor was dispatched virtually";
}

TEST_F(SweepTest, SweepingDoesNotDispatchThroughTheMessagesVtable)
{
    // Same, on the path that matters most: the app is gone, so the vtable this
    // would dispatch through is exactly the memory that has been freed.
    DestructorWatchMessage::sDestroyed = false;

    auto* msg = mMgr->allocateMessage<DestructorWatchMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_FALSE(DestructorWatchMessage::sDestroyed)
            << "the sweep dispatched the destructor virtually";
}

TEST_F(SweepTest, SweepingAProcessWithNothingOutstandingDoesNothing)
{
    const size_t before = freeBlocks(pool());
    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 0u);
    EXPECT_EQ(freeBlocks(pool()), before);
}

TEST_F(SweepTest, ASoleReferenceIsReclaimedAndTheBlockComesBack)
{
    const size_t before = freeBlocks(pool());

    auto* msg = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);
    ASSERT_EQ(freeBlocks(pool()), before - 1u);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before) << "block was not returned to the pool";
}

TEST_F(SweepTest, AReferenceSomebodyElseStillHoldsSurvives)
{
    // The property that makes the sweep safe to run at all: it drops exactly
    // the dead process's reference and leaves everyone else's alone.
    const size_t before = freeBlocks(pool());

    auto* msg = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);
    mMgr->retainMessage(msg);          // a second holder -- a queue, say
    mMgr->addMessageOwner(msg, kPidB);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before - 1u) << "freed while still referenced";

    // And when the other holder lets go, it does come back.
    EXPECT_EQ(mMgr->releaseByOwner(kPidB), 1u);
    EXPECT_EQ(freeBlocks(pool()), before);
}

TEST_F(SweepTest, TwoReferencesOwedByOneProcessAreBothReclaimed)
{
    const size_t before = freeBlocks(pool());

    auto* msg = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);  // allocated it
    mMgr->retainMessage(msg);
    mMgr->addMessageOwner(msg, kPidA);  // and received it back

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 2u);
    EXPECT_EQ(freeBlocks(pool()), before) << "one of the two references leaked";
}

TEST_F(SweepTest, ABlockClaimedButNeverConstructedIsReturnedWithoutBeingReleased)
{
    // The process was killed between the pool handing out the block and the
    // placement new. There is no object: the count is zero and so is the first
    // word. Releasing it would decrement from zero -- tripping the DEBUG
    // double-release assert on a legitimate reclaim, and in release wrapping
    // the count so the block could never be freed OR swept again.
    const size_t before = freeBlocks(pool());

    void* raw = mMgr->allocateRawMemory(sizeof(TestMessage));
    ASSERT_NE(raw, nullptr);
    ASSERT_EQ(freeBlocks(pool()), before - 1u);
    mMgr->addMessageOwner(static_cast<SDK::MessageBase*>(raw), kPidA);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before) << "unconstructed block was not reclaimed";
}

TEST_F(SweepTest, APartBuiltBlockIsStillReclaimed)
{
    // Terminated inside the constructor, after the vptr was stored but before
    // the reference count was set to 1. The count reads 0 and the first word
    // does not -- but the pool still has the block handed out, so there is no
    // ambiguity: no message was ever finished in it, and it comes back.
    const size_t before = freeBlocks(pool());

    void* raw = mMgr->allocateRawMemory(sizeof(TestMessage));
    ASSERT_NE(raw, nullptr);

    // Stand in for a half-run constructor: a plausible vptr, count still zero.
    uintptr_t fakeVptr = 0xDEADBEEFu;
    memcpy(raw, &fakeVptr, sizeof(fakeVptr));

    mMgr->addMessageOwner(static_cast<SDK::MessageBase*>(raw), kPidA);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before) << "part-built block was not reclaimed";
}

TEST_F(SweepTest, AClaimOnAnAlreadyFreeBlockIsRefusedNotActedOn)
{
    // The case that decides how the sweep is written. A claim must never
    // outlive its block, but if one ever did, returning that block again would
    // push it onto the free list twice: the list goes cyclic and the pool
    // hands one block to two owners, with nothing detecting it.
    //
    // It cannot be inferred from the block's contents. The first word of a
    // free block is its free-list link, and that is NULL for whichever block
    // is at the bottom of the list -- so a freed block can look exactly like a
    // never-constructed one. The pool is asked instead.
    void* raw = mMgr->allocateRawMemory(sizeof(TestMessage));
    ASSERT_NE(raw, nullptr);
    mMgr->getPool().deallocate(raw);          // straight back to the free list

    const size_t before = freeBlocks(pool());
    mMgr->addMessageOwner(static_cast<SDK::MessageBase*>(raw), kPidA);

    // Accounted for, so it is visible, but the block itself is untouched.
    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before)
            << "an already-free block was returned to the pool a second time";
}

TEST_F(SweepTest, SweepingIsIdempotent)
{
    const size_t before = freeBlocks(pool());

    auto* msg = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    // A second sweep must not find the block again. If it did it would release
    // memory that is already on the free list -- and the pool has no
    // double-free detection, so the block would simply be handed to two owners.
    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 0u);
    EXPECT_EQ(freeBlocks(pool()), before);
}

TEST_F(SweepTest, AReleasedMessageIsNoLongerSweepable)
{
    const size_t before = freeBlocks(pool());

    auto* msg = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(msg, nullptr);
    mMgr->addMessageOwner(msg, kPidA);

    // The ordinary path: the owner gives the claim back, then the reference.
    mMgr->removeMessageOwner(msg, kPidA);
    mMgr->releaseMessage(msg);
    ASSERT_EQ(freeBlocks(pool()), before);

    // The block is on the free list now. A sweep that still found it would
    // hand back free memory.
    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 0u);
    EXPECT_EQ(freeBlocks(pool()), before);
}

TEST_F(SweepTest, ManyOutstandingBlocksAreAllReclaimed)
{
    const size_t before = freeBlocks(pool());
    constexpr size_t kCount = 40;

    for (size_t i = 0; i < kCount; ++i) {
        auto* msg = mMgr->allocateMessage<TestMessage>();
        ASSERT_NE(msg, nullptr) << "at " << i;
        mMgr->addMessageOwner(msg, kPidA);
    }
    ASSERT_EQ(freeBlocks(pool()), before - kCount);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), kCount);
    EXPECT_EQ(freeBlocks(pool()), before);
}

TEST_F(SweepTest, OnlyTheNamedProcessIsSwept)
{
    const size_t before = freeBlocks(pool());

    auto* a = mMgr->allocateMessage<TestMessage>();
    auto* b = mMgr->allocateMessage<TestMessage>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    mMgr->addMessageOwner(a, kPidA);
    mMgr->addMessageOwner(b, kPidB);

    EXPECT_EQ(mMgr->releaseByOwner(kPidA), 1u);
    EXPECT_EQ(freeBlocks(pool()), before - 1u) << "swept the wrong process's block";

    EXPECT_EQ(mMgr->releaseByOwner(kPidB), 1u);
    EXPECT_EQ(freeBlocks(pool()), before);
}

} // namespace
