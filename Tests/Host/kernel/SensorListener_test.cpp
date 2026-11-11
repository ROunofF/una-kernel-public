/**
 * @file SensorListener_test.cpp
 * @brief SensorListener delivers without waiting on a full app queue.
 *
 * The listener is called under the sensor driver's mutex, and from the sweep
 * under the sensor manager's too. DualAppCommPushTimeout_test covers what a
 * zero push timeout does; this pins that the listener is the caller that asks
 * for it. With the default wait each sample on a full queue would take
 * SEND_TIMEOUT_MS with those locks held.
 */

#include <gtest/gtest.h>

#include "App/DualAppComm.hpp"
#include "App/MessageManager.hpp"
#include "App/SensorListener.hpp"
#include "OS.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/SensorData.hpp"

#include <chrono>
#include <cstdint>

namespace {

using App::DualAppComm;
using App::MessageManager;
using App::SensorListener;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kSrvPID = 0x3400'0000u;
constexpr uint32_t kGuiPID = 0x3400'0001u;
constexpr uint16_t kHandle = 7;
constexpr uint16_t kStride = sizeof(SDK::Sensor::Data);

struct KernelMessage : public SDK::MessageBase {
    KernelMessage() : SDK::MessageBase(static_cast<SDK::MessageType::Type>(0x0318'0000u)) {}
};

class SensorListenerTest : public ::testing::Test {
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

    void fill(bool toService, size_t depth)
    {
        for (size_t i = 0; i < depth; ++i) {
            auto* msg = mMgr->allocateMessage<KernelMessage>();
            ASSERT_NE(msg, nullptr);
            const bool ok = toService ? mComm->sendToService(msg, 0) : mComm->sendToGui(msg, 0);
            mMgr->releaseMessage(msg);
            ASSERT_TRUE(ok) << "filling, at " << i;
        }
    }

    /// Milliseconds taken by `count` one-sample deliveries.
    double timeDeliveries(SensorListener& listener, int count)
    {
        SDK::Sensor::Data sample{};
        const auto t0 = Clock::now();
        for (int i = 0; i < count; ++i) {
            listener.onSdlNewData(kHandle, &sample, 1, kStride);
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    /// Delivery onto a full queue: returns without the default wait, having
    /// yielded once per sample - the zero-timeout path, not the default one.
    void expectNoWaitOnFullQueue(bool toService, size_t depth)
    {
        fill(toService, depth);
        SensorListener listener(*mComm, toService);

        // Twenty samples that each waited the default would take 200 ms; half
        // that is a generous bound for twenty that do not wait.
        constexpr int kSamples = 20;
        OS::gYields = 0;
        const double ms = timeDeliveries(listener, kSamples);
        EXPECT_LT(ms, kSamples * DualAppComm::SEND_TIMEOUT_MS / 2.0);
        EXPECT_EQ(OS::gYields.load(), static_cast<uint32_t>(kSamples));
    }

    MessageManager* mMgr  = nullptr;
    DualAppComm*    mComm = nullptr;
};

}

TEST_F(SensorListenerTest, DeliversASampleToTheServiceWhenThereIsRoom)
{
    SensorListener listener(*mComm, true);

    SDK::Sensor::Data sample{};
    sample.mTimeStamp    = 1234;
    sample.mValue[0].u32 = 0xCAFEu;
    OS::gYields = 0;
    listener.onSdlNewData(kHandle, &sample, 1, kStride);
    EXPECT_EQ(OS::gYields.load(), 0u) << "a push that fits never yields";

    auto& srv = *mComm->getServiceComm();
    SDK::MessageBase* msg = nullptr;
    ASSERT_TRUE(srv.getMessage(msg, 0));
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->getType(), SDK::MessageType::EVENT_SENSOR_LAYER_DATA);

    const auto* event = static_cast<const SDK::Message::Sensor::EventData*>(msg);
    EXPECT_EQ(event->handle, kHandle);
    EXPECT_EQ(event->count, 1u);
    EXPECT_EQ(event->stride, kStride);
    EXPECT_EQ(event->data[0].mTimeStamp, 1234u);
    EXPECT_EQ(event->data[0].mValue[0].u32, 0xCAFEu);
    srv.releaseMessage(msg);
}

TEST_F(SensorListenerTest, DoesNotWaitOnAFullServiceQueue)
{
    expectNoWaitOnFullQueue(true, DualAppComm::SERVICE_QUEUE_SIZE);
}

TEST_F(SensorListenerTest, DoesNotWaitOnAFullGuiQueue)
{
    expectNoWaitOnFullQueue(false, DualAppComm::GUI_QUEUE_SIZE);
}
