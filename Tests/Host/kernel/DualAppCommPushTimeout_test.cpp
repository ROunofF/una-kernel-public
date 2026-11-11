/**
 * @file DualAppCommPushTimeout_test.cpp
 * @brief How long a kernel-to-app send waits on a full queue.
 *
 * Sensor delivery runs under the driver's mutex, and from the sweep under the
 * sensor manager's too. If its push waits for room in a full queue, it waits
 * with those held, and one app that has stopped draining stalls every other
 * sensor client and the teardown that needs those locks. So sensor delivery
 * must be able to ask for no wait at all, while every other caller keeps the
 * wait it has today. SensorListener_test pins that it does ask.
 *
 * The host Queue honours timeouts for real - zero tests and returns, anything
 * else parks - so elapsed time is what shows which one a send asked for.
 */

#include <gtest/gtest.h>

#include "App/DualAppComm.hpp"
#include "App/MessageManager.hpp"
#include "OS.hpp"
#include "SDK/Messages/MessageBase.hpp"

#include <chrono>
#include <cstdint>

namespace {

using App::DualAppComm;
using App::MessageManager;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kSrvPID = 0x3300'0000u;
constexpr uint32_t kGuiPID = 0x3300'0001u;

struct KernelMessage : public SDK::MessageBase {
    KernelMessage() : SDK::MessageBase(static_cast<SDK::MessageType::Type>(0x0318'0000u)) {}
};

class PushTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mMgr  = new MessageManager();
        mComm = new DualAppComm(kSrvPID, kGuiPID, *mMgr);
    }

    void TearDown() override
    {
        delete mComm;   // drains the queues back to the pool
        delete mMgr;
    }

    /// One kernel-side send, holding the caller's reference only for the call
    /// - the shape SensorListener has.
    bool sendOne(bool toService, uint32_t pushTimeoutMs, bool useDefault)
    {
        auto* msg = mMgr->allocateMessage<KernelMessage>();
        EXPECT_NE(msg, nullptr);
        if (msg == nullptr) {
            return false;
        }
        bool ok;
        if (toService) {
            ok = useDefault ? mComm->sendToService(msg)
                            : mComm->sendToService(msg, pushTimeoutMs);
        } else {
            ok = useDefault ? mComm->sendToGui(msg)
                            : mComm->sendToGui(msg, pushTimeoutMs);
        }
        mMgr->releaseMessage(msg);
        return ok;
    }

    void fill(bool toService, size_t depth)
    {
        for (size_t i = 0; i < depth; ++i) {
            ASSERT_TRUE(sendOne(toService, 0, false)) << "filling, at " << i;
        }
    }

    /// Milliseconds taken by `count` sends that are expected to fail.
    double timeFailingSends(bool toService, int count, uint32_t pushTimeoutMs, bool useDefault)
    {
        const auto t0 = Clock::now();
        for (int i = 0; i < count; ++i) {
            EXPECT_FALSE(sendOne(toService, pushTimeoutMs, useDefault));
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    MessageManager* mMgr  = nullptr;
    DualAppComm*    mComm = nullptr;
};

}

TEST_F(PushTimeoutTest, ZeroTimeoutStillDeliversWhenThereIsRoom)
{
    EXPECT_TRUE(sendOne(true, 0, false));
    EXPECT_TRUE(sendOne(false, 0, false));
}

TEST_F(PushTimeoutTest, ZeroTimeoutReturnsAtOnceOnAFullServiceQueue)
{
    fill(true, DualAppComm::SERVICE_QUEUE_SIZE);

    // Twenty sends that each waited the default would take 200 ms. Half that
    // is a generous bound for twenty that do not wait at all, and far from
    // flaky on a loaded CI runner.
    constexpr int kSends = 20;
    const double ms = timeFailingSends(true, kSends, 0, false);
    EXPECT_LT(ms, kSends * DualAppComm::SEND_TIMEOUT_MS / 2.0);
}

TEST_F(PushTimeoutTest, ZeroTimeoutReturnsAtOnceOnAFullGuiQueue)
{
    fill(false, DualAppComm::GUI_QUEUE_SIZE);

    constexpr int kSends = 20;
    const double ms = timeFailingSends(false, kSends, 0, false);
    EXPECT_LT(ms, kSends * DualAppComm::SEND_TIMEOUT_MS / 2.0);
}

TEST_F(PushTimeoutTest, ZeroTimeoutYieldsOnceBeforeGivingUp)
{
    // Sensor delivery runs at the same priority as the app threads, so a push
    // that failed and returned at once would never let an app that is merely
    // behind catch up. One yield gives it that chance - and only one, so an
    // app that has stopped costs one yield per sample, not a wait.
    fill(true, DualAppComm::SERVICE_QUEUE_SIZE);

    OS::gYields = 0;
    EXPECT_FALSE(sendOne(true, 0, false));
    EXPECT_EQ(OS::gYields.load(), 1u);
}

TEST_F(PushTimeoutTest, ZeroTimeoutYieldsOnceOnAFullGuiQueueToo)
{
    fill(false, DualAppComm::GUI_QUEUE_SIZE);

    OS::gYields = 0;
    EXPECT_FALSE(sendOne(false, 0, false));
    EXPECT_EQ(OS::gYields.load(), 1u);
}

TEST_F(PushTimeoutTest, TheRetryDeliversWhenTheReceiverDrainsDuringTheYield)
{
    // The case the yield exists for: a receiver that is behind, not stopped.
    // Here it takes one message while the sender has given up the CPU, and
    // the retry must then land in the room that made.
    fill(true, DualAppComm::SERVICE_QUEUE_SIZE);

    auto& srv = *mComm->getServiceComm();
    OS::gOnYield = [&srv] {
        SDK::MessageBase* taken = nullptr;
        if (srv.getMessage(taken, 0) && taken != nullptr) {
            srv.releaseMessage(taken);
        }
    };
    OS::gYields = 0;

    EXPECT_TRUE(sendOne(true, 0, false));
    EXPECT_EQ(OS::gYields.load(), 1u);

    OS::gOnYield = nullptr;
}

TEST_F(PushTimeoutTest, OnlyAFailedZeroTimeoutPushYields)
{
    OS::gYields = 0;
    EXPECT_TRUE(sendOne(true, 0, false));
    EXPECT_EQ(OS::gYields.load(), 0u) << "a push that fits never yields";

    fill(true, DualAppComm::SERVICE_QUEUE_SIZE - 1);
    OS::gYields = 0;
    EXPECT_FALSE(sendOne(true, 0, true));
    EXPECT_EQ(OS::gYields.load(), 0u) << "the default waits instead of yielding";
}

// Every other caller keeps the wait it had: a queue that is only briefly full
// should not lose a lifecycle message or a notification.
TEST_F(PushTimeoutTest, DefaultStillWaitsForRoomInTheServiceQueue)
{
    fill(true, DualAppComm::SERVICE_QUEUE_SIZE);
    EXPECT_GE(timeFailingSends(true, 1, 0, true), DualAppComm::SEND_TIMEOUT_MS - 1.0);
}

TEST_F(PushTimeoutTest, DefaultStillWaitsForRoomInTheGuiQueue)
{
    fill(false, DualAppComm::GUI_QUEUE_SIZE);
    EXPECT_GE(timeFailingSends(false, 1, 0, true), DualAppComm::SEND_TIMEOUT_MS - 1.0);
}
