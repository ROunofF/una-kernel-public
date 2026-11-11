/**
 * @file DualAppCommOwnership_test.cpp
 * @brief Where claims are actually staked and given back.
 *
 * MessagePool_test.cpp proves the claim table works and MessageManagerSweep_test
 * proves the sweep does the right thing with a claimed block. Neither touches
 * the wiring, which is where the invariant is either true or not:
 *
 *   - a process claims when it allocates, and when it POPS a message;
 *   - it unclaims when it releases;
 *   - a send claims NOTHING, because the reference it retains belongs to the
 *     queue, not to the receiver.
 *
 * That last one is the whole reason claims are taken on the pop. Staking one on
 * the push would mark a message still sitting in a queue as the receiver's, and
 * a sweep would then free it out from under the queue. Without these tests,
 * deleting the claim in getMessage -- which reinstates the original leak
 * wholesale -- passes every other test in the suite.
 */

#include <gtest/gtest.h>

#include "App/DualAppComm.hpp"
#include "App/MessageManager.hpp"
#include "SDK/Messages/MessageBase.hpp"

namespace {

using App::DualAppComm;
using App::MessageManager;

constexpr uint32_t kSrvPID = 0x2200'0000u;
constexpr uint32_t kGuiPID = 0x2200'0001u;

/// An app-specific type, so sendMessage takes the direct service<->GUI route
/// rather than the common queue to the kernel.
constexpr SDK::MessageType::Type kAppSpecific =
        static_cast<SDK::MessageType::Type>(0x0000'0042u);

struct AppMessage : public SDK::MessageBase {
    AppMessage() : SDK::MessageBase(kAppSpecific) {}
};

class OwnershipTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mMgr  = new MessageManager();
        mComm = new DualAppComm(kSrvPID, kGuiPID, *mMgr);
    }

    void TearDown() override
    {
        delete mComm;
        delete mMgr;
    }

    uint8_t pool() const
    {
        return mMgr->getPool().findPoolForSize(sizeof(AppMessage));
    }

    size_t freeBlocks() const { return mMgr->getPool().getFreeCount(pool()); }

    SDK::Interface::IAppComm& srv() { return *mComm->getServiceComm(); }
    SDK::Interface::IAppComm& gui() { return *mComm->getGuiComm(); }

    MessageManager* mMgr  = nullptr;
    DualAppComm*    mComm = nullptr;
};

TEST_F(OwnershipTest, AllocatingClaimsForTheAllocator)
{
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);

    // Killed here, the service still owes this block. Nothing else knows it
    // exists, which is the original bug.
    EXPECT_EQ(mMgr->releaseByOwner(kSrvPID), 1u);
    EXPECT_EQ(freeBlocks(), before);
}

TEST_F(OwnershipTest, ReleasingGivesTheClaimBack)
{
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    srv().releaseMessage(msg);
    ASSERT_EQ(freeBlocks(), before);

    // Nothing left owed, so a sweep must find nothing. If releaseMessage left
    // the claim behind, this would hand back a block that is already free.
    EXPECT_EQ(mMgr->releaseByOwner(kSrvPID), 0u);
    EXPECT_EQ(freeBlocks(), before);
}

TEST_F(OwnershipTest, AQueuedMessageIsNotYetTheReceiversToReturn)
{
    // The property that makes claim-on-pop necessary. After the send the
    // message sits in the GUI's queue, but the GUI owes nothing: it has not
    // taken it. A sweep of the GUI here must not touch the block, or it would
    // free it out from under the queue.
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    ASSERT_TRUE(srv().sendMessage(msg, 0));

    EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 0u) << "swept a message the GUI had not taken";
    EXPECT_EQ(freeBlocks(), before - 1u) << "block was freed while still queued";

    // The message is still deliverable.
    SDK::MessageBase* got = nullptr;
    EXPECT_TRUE(gui().getMessage(got, 0));
    EXPECT_EQ(got, msg);
}

TEST_F(OwnershipTest, PoppingClaimsForTheReceiver)
{
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    ASSERT_TRUE(srv().sendMessage(msg, 0));
    srv().releaseMessage(msg);          // sender done; queue + GUI to come

    SDK::MessageBase* got = nullptr;
    ASSERT_TRUE(gui().getMessage(got, 0));

    // This is the case the original leak was about: the GUI has taken the message out of the
    // queue and parked it somewhere the kernel cannot see. Only the claim taken
    // on the pop can get it back.
    EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 1u);
    EXPECT_EQ(freeBlocks(), before) << "block not reclaimed from the dead GUI";
}

TEST_F(OwnershipTest, TheReceiverReleasingReturnsTheBlock)
{
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    ASSERT_TRUE(srv().sendMessage(msg, 0));
    srv().releaseMessage(msg);

    SDK::MessageBase* got = nullptr;
    ASSERT_TRUE(gui().getMessage(got, 0));
    gui().releaseMessage(got);

    EXPECT_EQ(freeBlocks(), before);
    EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 0u);
    EXPECT_EQ(mMgr->releaseByOwner(kSrvPID), 0u);
}

TEST_F(OwnershipTest, ReleasingClearsTheClaimEvenWhenTheBlockSurvives)
{
    // The one line this pins is the unclaim in InternalAppComm::releaseMessage.
    // Every other test that releases either drops the LAST reference -- so
    // deallocate()'s belt-and-braces clearOwners() wipes the claim anyway and
    // the omission is invisible -- or never sweeps that PID afterwards. Here
    // the block deliberately stays alive on the queue's reference, so a claim
    // left behind is still standing when the sweep runs, and the sweep frees a
    // message out from under the queue.
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    ASSERT_TRUE(srv().sendMessage(msg, 0));
    srv().releaseMessage(msg);          // block lives on: the queue holds it

    ASSERT_EQ(freeBlocks(), before - 1u);

    EXPECT_EQ(mMgr->releaseByOwner(kSrvPID), 0u)
            << "the service still owed something after releasing";
    EXPECT_EQ(freeBlocks(), before - 1u)
            << "swept a queued message the service had already released";

    // And it is still deliverable and still returns normally.
    SDK::MessageBase* got = nullptr;
    ASSERT_TRUE(gui().getMessage(got, 0));
    EXPECT_EQ(got, msg);
    gui().releaseMessage(got);
    EXPECT_EQ(freeBlocks(), before);
}

TEST_F(OwnershipTest, SweepingOneHalfLeavesTheOthersReferenceAlone)
{
    // Both halves owe this block: the service allocated it and has not let go,
    // the GUI has taken it off the queue. Killing the GUI must not destroy a
    // block the service is still using.
    const size_t before = freeBlocks();

    auto* msg = srv().allocateMessage<AppMessage>();
    ASSERT_NE(msg, nullptr);
    ASSERT_TRUE(srv().sendMessage(msg, 0));

    SDK::MessageBase* got = nullptr;
    ASSERT_TRUE(gui().getMessage(got, 0));

    EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 1u);
    EXPECT_EQ(freeBlocks(), before - 1u) << "freed while the service still held it";

    // And the service's own reference still returns it.
    srv().releaseMessage(msg);
    EXPECT_EQ(freeBlocks(), before);
}

TEST_F(OwnershipTest, AFailedSendLeavesNoClaimBehind)
{
    // Fill the GUI queue, then send once more. The push fails, the sender still
    // holds its allocation reference, and the GUI must owe nothing for a
    // message it never received.
    const size_t before = freeBlocks();

    std::vector<SDK::MessageBase*> sent;
    bool sawFailedSend = false;
    for (;;) {
        auto* m = srv().allocateMessage<AppMessage>();
        if (m == nullptr) {
            break;                       // pool exhausted before the queue filled
        }
        if (!srv().sendMessage(m, 0)) {
            sawFailedSend = true;
            // The push failed. Only the allocation claim should remain.
            EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 0u)
                    << "a failed send left the receiver owing something";
            srv().releaseMessage(m);
            break;
        }
        sent.push_back(m);
        srv().releaseMessage(m);        // hand it to the queue outright
    }

    ASSERT_FALSE(sent.empty());
    // Without this the test passes while asserting nothing, if the queue ever
    // becomes larger than the pool.
    ASSERT_TRUE(sawFailedSend)
            << "the pool ran out before the queue filled; this no longer tests a failed send";

    // Drain what did land, so the fixture tears down clean.
    for (size_t i = 0; i < sent.size(); ++i) {
        SDK::MessageBase* got = nullptr;
        if (gui().getMessage(got, 0)) {
            gui().releaseMessage(got);
        }
    }
    EXPECT_EQ(freeBlocks(), before);
}

TEST_F(OwnershipTest, EachHalfClaimsOnlyItsOwnPid)
{
    const size_t before = freeBlocks();

    auto* s = srv().allocateMessage<AppMessage>();
    auto* g = gui().allocateMessage<AppMessage>();
    ASSERT_NE(s, nullptr);
    ASSERT_NE(g, nullptr);

    EXPECT_EQ(mMgr->releaseByOwner(kSrvPID), 1u);
    EXPECT_EQ(freeBlocks(), before - 1u) << "swept the GUI's block too";

    EXPECT_EQ(mMgr->releaseByOwner(kGuiPID), 1u);
    EXPECT_EQ(freeBlocks(), before);
}

} // namespace
