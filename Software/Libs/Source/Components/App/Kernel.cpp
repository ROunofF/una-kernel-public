/**
 ******************************************************************************
 * @file    AppKernel.cpp
 * @brief   Concrete Kernel implementation.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "SDK/Messages/CommandMessages.hpp"

#include "App/Kernel.hpp"
#include "App/MessageManager.hpp"
#include "OS.hpp"

#define LOG_MODULE_PRX      "App.Kernel"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace App
{

// Bounded retry budgets for exit(): the shared message pool / common queue can
// be transiently full, but exit() must never spin unbounded.
static constexpr uint32_t skTerminateAllocRetries = 5;
static constexpr uint32_t skTerminateSendRetries  = 5;
static constexpr uint32_t skTerminateRetryMs      = 100;

Kernel::Kernel(const char *name,
              SDK::Interface::IAppComm &comm,
              App::MessageManager &msgMgr,
              SDK::Interface::IFileSystem &fs)
    : mName(name)
    , mLogger(mName)
    , mMemAllocator(mName)
    , mComm(comm) , mMessageManager(msgMgr), mFs(fs), mIKernel(*this)
    , isReadyToDelete(false)
    , mTerminateMsg(nullptr)
{
    // Reserve the terminate message up front, while the pool still has room, so
    // exit() can always signal termination without allocating.
    //
    // Allocated through the MessageManager, NOT through mComm. mComm records
    // the process as owing whatever it allocates, and this block is held by
    // this kernel-side object for the whole life of the AppInstance -- it
    // outlives the process. A process that dies without calling exit() (a load
    // failure, the GUI watchdog, forceTerminateService) is swept, and a claim
    // here would let that sweep destroy the reserve while mTerminateMsg still
    // points at it. The next launch would then push a free block onto the
    // queue, and ~Kernel would release it a second time.
    mTerminateMsg = mMessageManager.allocateMessage<SDK::Message::RequestAppTerminate>();
    if (mTerminateMsg == nullptr) {
        LOG_WARNING("[%s] Could not reserve terminate message\n", mName);
    }
}

Kernel::~Kernel()
{
    // Release the reserved message. By this point ~AppInstance has already
    // joined the dispatch thread and cleared the queues, so any in-flight copy
    // of this message has had its queue reference dropped -- only our reserve
    // reference remains. It carries no owner claim (see the constructor), so
    // no sweep can have taken it from under us.
    if (mTerminateMsg != nullptr) {
        mMessageManager.releaseMessage(mTerminateMsg);
        mTerminateMsg = nullptr;
    }
}

void Kernel::reset()
{
    mMemAllocator.freeAll();
    isReadyToDelete = false;
}

void Kernel::waitReadyToDelete()
{
    uint32_t cnt = 10;
    while (!isReadyToDelete && cnt-- > 0) {
        delay(10);
    }
}

// IKIP
void* Kernel::queryInterface(IntfID iid)
{
    SDK::Interface::ISystem *isystem = this;

    switch (iid) {
        case IntfID::IID_SYSTEM:     return isystem;
        case IntfID::IID_LOGGER:     return &mLogger;
        case IntfID::IID_APP_MEMORY: return &mMemAllocator;
        case IntfID::IID_APP_COMM:   return &mComm;
        case IntfID::IID_FILESYSTEM: return &mFs;

        default:
            LOG_ERROR("[%s] Interface ID 0x%08X not found\n", mName, iid);
            return nullptr;
    }
}

// ISystem
void Kernel::exit(int status)
{
    if (status != 0) {
        LOG_ERROR("[%s] Abnormal exit. Status %d\n", mName, status);
    }

    // Prefer the reserved message (no allocation). Fall back to a bounded
    // allocation only if the reserve was never obtained (pool full at load).
    SDK::MessageBase* msg = mTerminateMsg;
    const bool ownsMsg = (msg == nullptr);  // fallback allocations we must free
    if (msg == nullptr) {
        for (uint32_t i = 0; i < skTerminateAllocRetries && msg == nullptr; ++i) {
            msg = mComm.allocateMessage<SDK::Message::RequestAppTerminate>();
            if (msg == nullptr) {
                delay(skTerminateRetryMs);
            }
        }
    }

    if (msg != nullptr) {
        // Bounded send retry. The common queue is drained by the (independent)
        // kernel dispatch thread, so a transiently full queue clears quickly.
        for (uint32_t i = 0; i < skTerminateSendRetries; ++i) {
            if (mComm.sendMessage(msg, 0)) {
                break;
            }
            delay(skTerminateRetryMs);
        }

        // The reserved message stays Kernel-owned for reuse (the queue holds its
        // own reference); only a fallback allocation is released here.
        if (ownsMsg) {
            mComm.releaseMessage(msg);
        }
        LOG_INFO("[%s] Waiting for termination...\n", mName);
    } else {
        // Could not even allocate a fallback. If the stop was kernel-initiated,
        // the stop watchdog force-terminates us; otherwise we park quietly.
        LOG_ERROR("[%s] No terminate message; awaiting force-terminate\n", mName);
    }

    isReadyToDelete = true;
    for (;;) {
        delay(1000);
    }

    // No return
}

uint32_t Kernel::getTimeMs()
{
    return OS::GetTimeMs();
}

void Kernel::delay(uint32_t ms)
{
    OS::Delay(ms);
}

void Kernel::yield()
{
    OS::Yield();
}


} // namespace App2
