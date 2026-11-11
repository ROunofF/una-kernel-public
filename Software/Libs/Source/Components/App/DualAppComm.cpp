/**
 * @file DualAppComm.cpp
 * @brief Dual-part application communication (Service + GUI)
 *
 */

#include "App/DualAppComm.hpp"
#include "OS.hpp"

#define LOG_MODULE_PRX      "App.DualAppComm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace App
{

namespace
{

/**
 * @brief Return every message left in a queue to the pool
 *
 * Skips nullptr entries: OS::Queue::interrupt() wakes a blocked pop by
 * pushing a zero-initialised element, and one can be left behind when the
 * dispatch loop breaks on shouldStop() with a real message queued ahead of
 * it. Today only the common queue is registered for interruption (see
 * getAppCancellable()), but every drain guards for it so a future
 * registration cannot turn a drain into a null dereference.
 */
template <size_t N>
void drainQueue(OS::Queue<SDK::MessageBase*, N> &queue,
                App::MessageManager &manager,
                const char *name,
                uint32_t pid)
{
    SDK::MessageBase* msg;
    while (queue.pop(msg, 0)) {
        if (msg == nullptr) {
            continue;
        }
        LOG_WARNING("Clear %s Message 0x%08X, PID 0x%08X.\n", name, msg->getType(), pid);
        manager.releaseMessage(msg);
    }
}

/**
 * @brief Push onto a kernel-to-app queue, honouring the caller's wait.
 *
 * A zero timeout never blocks, but on a full queue it yields once and tries
 * again before giving up. Sensor delivery, which is what passes 0,
 * runs at the same priority as the app threads - from the sweep, or for an
 * event-based source from an app's kernel-side thread - so a push that fails
 * and returns at once never lets an app that is merely behind catch up - it
 * would drop samples the app was about to make room for. One yield gives it
 * that chance. For an app that has stopped draining, each failed sample
 * costs at most one round of the other ready tasks at the same priority -
 * each up to a time slice, with the caller's mutexes still held - which is
 * still far short of the 10 ms wait it replaces.
 */
template <size_t N>
bool pushToApp(OS::Queue<SDK::MessageBase*, N> &queue,
               SDK::MessageBase* msg,
               uint32_t pushTimeoutMs)
{
    if (queue.push(msg, pushTimeoutMs)) {
        return true;
    }
    if (pushTimeoutMs != 0) {
        return false;
    }

    OS::Yield();
    return queue.push(msg, 0);
}

} // namespace

DualAppComm::DualAppComm(uint32_t servicePID,
                                 uint32_t guiPID, 
                                 App::MessageManager& msgManager)
    : mServicePID(servicePID)
    , mGuiPID(guiPID)
    , mMessageManager(msgManager)
    , mCommonQueue()
    , mServiceQueue()
    , mGuiQueue()
    , mServiceComm(servicePID, msgManager, mCommonQueue, mServiceQueue, mGuiQueue, true)
    , mGuiComm(guiPID, msgManager, mCommonQueue, mServiceQueue, mGuiQueue, false)
{
    mCommonQueue.init("AppCommon");
    mServiceQueue.init("AppService");
    mGuiQueue.init("AppGui");
}

DualAppComm::~DualAppComm()
{
    clearAllQueues();
    mCommonQueue.deinit();
    mServiceQueue.deinit();
    mGuiQueue.deinit();
}

bool DualAppComm::receiveFromApp(SDK::MessageBase*& msg, uint32_t timeoutMs)
{
    bool status = mCommonQueue.pop(msg, timeoutMs);
    if (msg) {
        LOG_DEBUG("Kernel received Message 0x%08X, from PID 0x%08X\n", msg->getType(), msg->getProcessId());
    } else {
        LOG_DEBUG("Kernel received Empty Message\n");
    }
    return status;
}

bool DualAppComm::sendToApp(SDK::MessageBase* msg, uint32_t timeoutMs)
{
    if (msg == nullptr) {
        return false;
    }

    // Create completion semaphore if needed
    if (timeoutMs > 0) {
        if (!mMessageManager.initCompletionSemaphore(msg)) {
            LOG_ERROR("Failed to send internal Message to app. Invalid Semaphore.\n");
            return false;
        }
    }
    LOG_DEBUG("Backend -> Kernel.  Message 0x%08X\n", msg->getType());
    mMessageManager.retainMessage(msg);
    bool success = mCommonQueue.push(msg, SEND_TIMEOUT_MS);

    if (!success) {
        // No owner to clear: a message bound for the kernel is the
        // kernel's to return, so nothing was ever tagged.
        // Send failed - cleanup semaphore
        if (msg->needsResponse()) {
            mMessageManager.cleanupCompletionSemaphore(msg);
        }
        mMessageManager.releaseMessage(msg);
        // Throttled; the first line keeps its old text.
        const LogThrottle::Decision d = mCommonFullLog.onFailure(OS::GetTimeMs());
        if (d.action == LogThrottle::Action::FIRST) {
            LOG_ERROR("Failed to send internal Message to app. Queue is full.\n");
        } else if (d.action == LogThrottle::Action::SUMMARY) {
            LOG_ERROR("Failed to send internal Message to app. Queue is full. %u more since the last report\n",
                    static_cast<unsigned>(d.suppressed));
        }
        return false;
    }

    if (const uint32_t failures = mCommonFullLog.onSuccess(OS::GetTimeMs)) {
        LOG_WARNING("Send of internal Message to app recovered after %u failures\n",
                static_cast<unsigned>(failures));
    }

    if (msg->needsResponse()) {
        bool completed = mMessageManager.waitCompletion(msg, timeoutMs);

        if (!completed) {
            // Timeout - message was sent but no response received
            msg->setResult(SDK::MessageResult::TIMEOUT);
            // Return true because send was successful (only response timed out)
        }
    }

    return true;
}

bool DualAppComm::sendToService(SDK::MessageBase* msg, uint32_t pushTimeoutMs)
{
    if (msg == nullptr) {
        return false;
    }
    // The wait for a RESPONSE, as opposed to pushTimeoutMs, the wait for room
    // in the queue. Always 0 here - waiting would deadlock - so no completion
    // semaphore is ever created, and the needsResponse() block after the push
    // finds none: waitCompletion() returns false and the message is marked
    // TIMEOUT once it is already queued.
    uint32_t responseTimeoutMs = 0;
    // Create completion semaphore if needed
    if (responseTimeoutMs > 0) {
        if (!mMessageManager.initCompletionSemaphore(msg)) {
            LOG_ERROR("Failed to send Message 0x%08X to PID 0x%08X. Invalid Semaphore.\n", msg->getType(), mServicePID);
            return false;
        }
    }

    LOG_DEBUG("Kernel  -> Service. Message 0x%08X to PID 0x%08X.\n", msg->getType(), mServicePID);
    // This reference belongs to the queue, not to the service: the service
    // owes nothing until it actually pops the message, and it claims it there
    // (InternalAppComm::getMessage). Claiming on the push instead would mean a
    // message still sitting in the queue looked like the service's, and a
    // sweep would free it out from under the queue.
    mMessageManager.retainMessage(msg);
    bool success = pushToApp(mServiceQueue, msg, pushTimeoutMs);

    if (!success) {
        // Nothing to unclaim: the receiver never had it, and a claim is only
        // taken by the receiver itself when it pops.
        // Send failed - cleanup semaphore
        if (msg->needsResponse()) {
            mMessageManager.cleanupCompletionSemaphore(msg);
        }
        // Read before the release: the log below must not depend on the
        // caller still holding a reference.
        [[maybe_unused]] const auto type = msg->getType();
        mMessageManager.releaseMessage(msg);
        // Throttled: a sensor feed into a service that has stopped
        // draining lands here once per sample. The first line keeps its old
        // text - log parsers match on it.
        const LogThrottle::Decision d = mServiceFullLog.onFailure(OS::GetTimeMs());
        if (d.action == LogThrottle::Action::FIRST) {
            LOG_ERROR("Failed to send Message 0x%08X to PID 0x%08X. Queue is full. %u\n",
                    type, mServicePID, mServiceQueue.size());
        } else if (d.action == LogThrottle::Action::SUMMARY) {
            // No message type: the count spans every type sent since the last
            // line, so naming the current one would misattribute it.
            LOG_ERROR("Failed to send Message to PID 0x%08X. Queue is full. %u. %u more since the last report\n",
                    mServicePID, mServiceQueue.size(),
                    static_cast<unsigned>(d.suppressed));
        }
        return false;
    }

    if (const uint32_t failures = mServiceFullLog.onSuccess(OS::GetTimeMs)) {
        LOG_WARNING("Send to PID 0x%08X recovered after %u failures\n",
                mServicePID, static_cast<unsigned>(failures));
    }

    if (msg->needsResponse()) {
        bool completed = mMessageManager.waitCompletion(msg, responseTimeoutMs);

        if (!completed) {
            // Timeout - message was sent but no response received
            msg->setResult(SDK::MessageResult::TIMEOUT);
            // Return true because send was successful (only response timed out)
        }
    }

    return true;
}

bool DualAppComm::sendToGui(SDK::MessageBase* msg, uint32_t pushTimeoutMs)
{
    if (msg == nullptr) {
        return false;
    }
    uint32_t responseTimeoutMs = 0;   // as in sendToService(): always 0
    // Create completion semaphore if needed
    if (responseTimeoutMs > 0) {
        if (!mMessageManager.initCompletionSemaphore(msg)) {
            LOG_ERROR("Failed to send Message 0x%08X to PID 0x%08X. Invalid Semaphore.\n", msg->getType(), mGuiPID);
            return false;
        }
    }

    LOG_DEBUG("Kernel  -> GUI.     Message 0x%08X to PID 0x%08X.\n", msg->getType(), mGuiPID);
    // As above: the queue holds this reference, and the GUI claims its own
    // when it pops. That is what covers the case this whole mechanism exists
    // for -- a message the GUI has taken and parked in the SDK's own user
    // queue, where no kernel drain can see it.
    mMessageManager.retainMessage(msg);
    bool success = pushToApp(mGuiQueue, msg, pushTimeoutMs);

    if (!success) {
        // Nothing to unclaim: the receiver never had it, and a claim is only
        // taken by the receiver itself when it pops.
        // Send failed - cleanup semaphore
        if (msg->needsResponse()) {
            mMessageManager.cleanupCompletionSemaphore(msg);
        }
        [[maybe_unused]] const auto type = msg->getType();   // before the release, as above
        mMessageManager.releaseMessage(msg);
        // Throttled, for the same reason as sendToService(): a GUI-side
        // sensor subscription reaches the same storm through here.
        const LogThrottle::Decision d = mGuiFullLog.onFailure(OS::GetTimeMs());
        if (d.action == LogThrottle::Action::FIRST) {
            LOG_ERROR("Failed to send Message 0x%08X to PID 0x%08X. Queue is full.\n",
                    type, mGuiPID);
        } else if (d.action == LogThrottle::Action::SUMMARY) {
            // No message type, for the same reason as in sendToService().
            LOG_ERROR("Failed to send Message to PID 0x%08X. Queue is full. %u more since the last report\n",
                    mGuiPID, static_cast<unsigned>(d.suppressed));
        }
        return false;
    }

    if (const uint32_t failures = mGuiFullLog.onSuccess(OS::GetTimeMs)) {
        LOG_WARNING("Send to PID 0x%08X recovered after %u failures\n",
                mGuiPID, static_cast<unsigned>(failures));
    }

    if (msg->needsResponse()) {
        bool completed = mMessageManager.waitCompletion(msg, responseTimeoutMs);

        if (!completed) {
            // Timeout - message was sent but no response received
            msg->setResult(SDK::MessageResult::TIMEOUT);
            // Return true because send was successful (only response timed out)
        }
    }

    return true;
}

void DualAppComm::clearServiceQueue()
{
    drainQueue(mServiceQueue, mMessageManager, "Service", getServicePID());
}

void DualAppComm::clearGuiQueue()
{
    drainQueue(mGuiQueue, mMessageManager, "GUI", getGuiPID());
}

void DualAppComm::clearCommonQueue()
{
    // PID 0 = the shared kernel-bound queue (messages from both processes).
    drainQueue(mCommonQueue, mMessageManager, "Common", 0);
}

void DualAppComm::clearAllQueues()
{
    // Clear GUI queue
    clearGuiQueue();

    // Clear Service queue
    clearServiceQueue();

    // Clear common queue
    clearCommonQueue();

    LOG_INFO("Queues cleared\n");
}

// =============================================================================
// InternalAppComm implementation
// =============================================================================

bool DualAppComm::InternalAppComm::sendMessage(SDK::MessageBase* msg, uint32_t timeoutMs)
{
    if (msg == nullptr) {
        return false;
    }
    
    // Set process ID
    msg->setProcessId(mPID);
    
    // Create completion semaphore if needed
    if (timeoutMs > 0) {
        if (!mMessageManager.initCompletionSemaphore(msg)) {
            LOG_ERROR("Failed to send Message 0x%08X from PID 0x%08X. Invalid Semaphore.\n", msg->getType(), mPID);
            return false;
        }
    }
    
    // Check if message is internal (Service <-> GUI direct routing)
    SDK::MessageType::Type msgType = msg->getType();
    bool success;
    mMessageManager.retainMessage(msg);

    // No claim is staked here. The sender keeps the one it took when it
    // allocated the message and gives it back in its own releaseMessage; the
    // receiver -- peer process or kernel -- takes its own when it pops. A
    // claim staked on the push would name a process that does not owe anything
    // yet, and a sweep of that process would free the block from under the
    // queue it is still sitting in.

    // One throttle per destination, so a process flooding its peer cannot
    // hide the first failure of a full kernel-bound queue.
    const bool toPeer = SDK::isApplicationSpecificMessage(msgType);
    LogThrottle& fullLog = toPeer ? mPeerFullLog : mKernelFullLog;

    if (toPeer) {
        // Internal message - route directly to other part
        if (mIsService) {
            // Service -> GUI direct
            LOG_DEBUG("Service -> GUI.     Message 0x%08X from PID 0x%08X.\n", msg->getType(), mPID);
            success = mGuiQueue.push(msg, SEND_TIMEOUT_MS);
        } else {
            // GUI -> Service direct
            LOG_DEBUG("GUI     -> Service. Message 0x%08X from PID 0x%08X.\n", msg->getType(), mPID);
            success = mServiceQueue.push(msg, SEND_TIMEOUT_MS);
        }
    } else {
        // Regular message - send to kernel via common queue
        LOG_DEBUG("%s -> Kernel.  Message 0x%08X from PID 0x%08X.\n", mIsService ? "Service" : "GUI    ", msg->getType(), mPID);
        success = mCommonQueue.push(msg, SEND_TIMEOUT_MS);
    }
    
    if (!success) {
        // Nothing to unclaim: the receiver never had it, and a claim is only
        // taken by the receiver itself when it pops.
        // Send failed - cleanup semaphore
        if (msg->needsResponse()) {
            mMessageManager.cleanupCompletionSemaphore(msg);
        }
        mMessageManager.releaseMessage(msg);
        // Throttled; an app sending faster than its peer or the kernel
        // drains would otherwise log once per message.
        // msgType was read before the push, so nothing here touches msg after
        // the release above.
        const LogThrottle::Decision d = fullLog.onFailure(OS::GetTimeMs());
        if (d.action == LogThrottle::Action::FIRST) {
            LOG_ERROR("Failed to send Message 0x%08X from PID 0x%08X. Queue is full.\n",
                    msgType, mPID);
        } else if (d.action == LogThrottle::Action::SUMMARY) {
            // The destination instead of the message type: the count spans
            // every type sent since the last line, but always one queue.
            LOG_ERROR("Failed to send Message from PID 0x%08X to %s. Queue is full. %u more since the last report\n",
                    mPID, toPeer ? "its peer" : "the kernel",
                    static_cast<unsigned>(d.suppressed));
        }
        return false;
    }

    if (const uint32_t failures = fullLog.onSuccess(OS::GetTimeMs)) {
        LOG_WARNING("Send from PID 0x%08X to %s recovered after %u failures\n",
                mPID, toPeer ? "its peer" : "the kernel",
                static_cast<unsigned>(failures));
    }

    if (msg->needsResponse()) {
        bool completed = mMessageManager.waitCompletion(msg, timeoutMs);

        if (!completed) {
            // Timeout - message was sent but no response received
            msg->setResult(SDK::MessageResult::TIMEOUT);
            // Return true because send was successful (only response timed out)
        }
    }

    return true;
}

bool DualAppComm::InternalAppComm::getMessage(SDK::MessageBase*& msg, uint32_t timeoutMs)
{
    // Service reads from Service queue, GUI reads from GUI queue
    bool status = false;
    if (mIsService) {
        status = mServiceQueue.pop(msg, timeoutMs);
        if (msg) {
            LOG_DEBUG("Service received Message 0x%08X\n", msg->getType());
        } else {
           LOG_DEBUG("Service received Empty Message\n");
        }
    } else {
        status = mGuiQueue.pop(msg, timeoutMs);
        if (msg) {
            LOG_DEBUG("GUI received Message 0x%08X\n", msg->getType());
        } else {
           LOG_DEBUG("GUI received Empty Message\n");
        }
    }

    // The handover point, and the reason the sweep needs no locking. Until
    // now the reference was the queue's; from here this process owes it, and
    // it owes it wherever the message then goes -- including into the SDK's
    // own user queue, which is where they were being lost.
    //
    // Claimed here rather than on the push because this runs on THIS process's
    // thread. Every claim for a PID is added and removed by that PID alone, so
    // once its thread is terminated its claims cannot CHANGE and a sweep has
    // nothing to race.
    //
    // Stable is not the same as complete. osThreadTerminate can land between
    // the pop above and this claim, between an allocation and its claim,
    // between releaseMessage clearing a claim and dropping the reference, or
    // inside a message constructor after the vptr is stored but before the
    // reference count is set (see MessageManager::releaseByOwner, which has to
    // recognise that block and decline to touch it). Each window is a few
    // instructions and each loses one block -- a leak, never a double free,
    // which is the right way round. So this bounds the loss to those windows
    // rather than eliminating it.
    if (status && msg != nullptr) {
        mMessageManager.addMessageOwner(msg, mPID);
    }

    return status;
}

void DualAppComm::InternalAppComm::sendResponse(SDK::MessageBase* msg)
{
    if (msg == nullptr || !msg->needsResponse()) {
        return;
    }
    
    mMessageManager.signalCompletion(msg);
}

} // namespace App
