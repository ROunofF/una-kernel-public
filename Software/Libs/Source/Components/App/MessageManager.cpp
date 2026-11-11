/**
 * @file MessageManager.cpp
 * @brief Implementation of message lifecycle manager
 */

#include "App/MessageManager.hpp"

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "OS.hpp"
#include <cstring>

#define LOG_MODULE_PRX      "App.MessageManager"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace App
{

namespace {

static_assert(POOL_COUNT == 4,
        "logPoolFree() reports four pools; update it for the new layout");

/// One line of free blocks per pool, so a capsule shows whether one pool ran
/// dry while the others had room, or all of them did. allocate() does not fall
/// back to a larger pool, so the two mean different things.
void logPoolFree(const MessagePool& pool)
{
    LOG_ERROR("Free blocks: %uB %u, %uB %u, %uB %u, %uB %u\n",
            static_cast<unsigned>(POOL_CONFIGS[0].blockSize), static_cast<unsigned>(pool.getFreeCount(0)),
            static_cast<unsigned>(POOL_CONFIGS[1].blockSize), static_cast<unsigned>(pool.getFreeCount(1)),
            static_cast<unsigned>(POOL_CONFIGS[2].blockSize), static_cast<unsigned>(pool.getFreeCount(2)),
            static_cast<unsigned>(POOL_CONFIGS[3].blockSize), static_cast<unsigned>(pool.getFreeCount(3)));
}

} // namespace

MessageManager::MessageManager()
    : mMessagePool()
{
}

void* MessageManager::allocateRawMemory(size_t size)
{
    uint8_t poolId = 0;
    void* ptr = mMessagePool.allocate(size, poolId, 0);

    // allocate() sets poolId even when it fails; POOL_COUNT means no pool fits.
    LogThrottle& throttle = mAllocFailLog[(poolId < POOL_COUNT) ? poolId : POOL_COUNT];

    if (ptr == nullptr) {
        // Throttled: an app that stops draining its queue makes this fail
        // once per sensor sample, for every app, until the watch resets.
        // The first line keeps its old text - log parsers match on it.
        const LogThrottle::Decision d = throttle.onFailure(OS::GetTimeMs());
        if (d.action == LogThrottle::Action::FIRST) {
            LOG_ERROR("Failed to allocate memory: size %u\n", static_cast<unsigned>(size));
            logPoolFree(mMessagePool);
        } else if (d.action == LogThrottle::Action::SUMMARY) {
            LOG_ERROR("Failed to allocate memory: size %u. %u more since the last report\n",
                    static_cast<unsigned>(size), static_cast<unsigned>(d.suppressed));
            logPoolFree(mMessagePool);
        }
        return nullptr;
    }

    // A success always has a real pool, so poolId indexes POOL_CONFIGS here.
    if (const uint32_t failures = throttle.onSuccess(OS::GetTimeMs)) {
        LOG_WARNING("Allocation from the %uB pool recovered after %u failures\n",
                static_cast<unsigned>(POOL_CONFIGS[poolId].blockSize),
                static_cast<unsigned>(failures));
    }

    // Zero memory
    memset(ptr, 0, size);
    LOG_DEBUG("Allocate memory: 0x%08X. size %d, \n", ptr, size);
    return ptr;
}

void MessageManager::releaseMessage(SDK::MessageBase* msg)
{
    if (msg == nullptr) {
        return;
    }

    // Decrement reference count atomically
    uint32_t oldCount = msg->mRefCount.fetch_sub(1);

    #ifdef DEBUG
    // Detect double release
    if (oldCount == 0) {
        configASSERT(false);
    }
    #endif

    // Check if last reference
    if (oldCount == 1) {
        // Cleanup completion semaphore if exists
        cleanupCompletionSemaphore(msg);

        // NON-VIRTUAL, and it has to be. A message allocated by an app is
        // placement-new'd inside that app (SDK IAppComm::allocateMessage), so
        // its vptr points into the app image -- and Loader::unload() delete[]s
        // that image. An unqualified msg->~MessageBase() loads the vptr and
        // dispatches through it, so destroying such a message after its app
        // has gone jumps into freed heap. That happens on the sweep path, and
        // also whenever the surviving half of an app releases a message its
        // dead peer had allocated -- which no reordering of the unload can
        // fix, because the release comes later and from another process.
        //
        // Safe because nothing below MessageBase declares a destructor: the
        // qualified call resolves statically to MessageBase::~MessageBase,
        // which lives in the kernel image and does nothing. A message type
        // that needs real destruction would break this, so it must not gain
        // one -- messages are fixed-layout PODs by ABI rule anyway.
        // Logged before BOTH the destructor and the free. After deallocate()
        // word 0 is a free-list link and the block may already have a new
        // tenant, so the type read there would be somebody else's message;
        // after the destructor the object's lifetime has formally ended, even
        // though getType() is non-virtual and the storage is untouched.
        LOG_DEBUG("Release memory: 0x%08X, type 0x%08X\n", msg, msg->getType());

        msg->MessageBase::~MessageBase();

        // Return memory to pool (pool determined automatically by address)
        mMessagePool.deallocate(msg);
    }
}

void MessageManager::addMessageOwner(SDK::MessageBase* msg, uint32_t pid)
{
    if (msg != nullptr) {
        (void)mMessagePool.addOwner(msg, pid);
    }
}

void MessageManager::removeMessageOwner(SDK::MessageBase* msg, uint32_t pid)
{
    if (msg == nullptr) {
        return;
    }

    if (!mMessagePool.removeOwner(msg, pid)) {
        // Two very different things land here.
        //
        // The benign one, and the only one reachable today: slot exhaustion.
        // A process that owes more references than could be recorded finds no
        // claim to give back on its last release. addOwner has already
        // reported that, so repeating it here would be noise -- and worse, it
        // would bury the other case behind a once-per-boot latch.
        //
        // The other one: this process is giving back a reference it never
        // claimed, which means it acquired it through one IAppComm and is
        // releasing it through another. That leaves the claim it did not clear
        // standing on a block whose reference is gone, and a claim outliving
        // its reference is the one input that can make a later sweep drop
        // somebody else's. Worth an error, because nothing else reports it --
        // and once per boot, because an app that does this does it on every
        // message.
        if (mMessagePool.getSlotExhaustionCount() == 0
            && !mOrphanReleaseLogged.exchange(true, std::memory_order_relaxed)) {
            LOG_ERROR("Release with no claim, PID 0x%08X, type 0x%08X\n",
                    (unsigned)pid, (unsigned)msg->getType());
        }
    }
}

size_t MessageManager::releaseByOwner(uint32_t pid)
{
    size_t dropped = 0;   // references given back for real messages
    size_t raws    = 0;   // blocks claimed at allocation but never constructed
    size_t stale   = 0;   // claimed a block the pool no longer has handed out
    size_t cursor  = 0;

    for (;;) {
        void* raw = mMessagePool.takeOwned(pid, cursor);
        if (raw == nullptr) {
            break;
        }

        SDK::MessageBase* msg = static_cast<SDK::MessageBase*>(raw);

        // A claim must name a block the pool still has handed out. If it
        // does not, the claim outlived its block -- it was not given back when
        // its reference was -- and acting on it would be catastrophic rather
        // than merely wrong: the block now belongs to somebody else, or is on
        // the free list, where returning it a second time makes the list
        // cyclic and hands one block to two owners. The pool does not detect
        // that. So this case is reported and otherwise left completely alone.
        //
        // Asked rather than inferred. The block cannot tell us: a free block's
        // first word is its free-list link, and that is NULL for whichever
        // block sits at the bottom of the list -- on a pool that has been
        // fully allocated, simply the first one freed afterwards. Reading zero
        // there and concluding "never constructed" is exactly the mistake that
        // would free it twice.
        //
        // NOTE this bounds the damage of a stale claim only while the block is
        // still free. If it has been freed AND handed out again before the
        // sweep runs, the flag reads true and the count belongs to the new
        // tenant, so the branch below would drop a reference that is not ours.
        // Nothing reachable produces a stale claim -- deallocate() clears every
        // claim before the block goes back -- but do not read this guard as
        // making one harmless.
        if (!mMessagePool.isAllocated(raw)) {
            ++stale;
            continue;
        }

        // The block is live and ours to deal with. A zero reference count now
        // unambiguously means no message was ever built in it: the process was
        // killed inside allocateMessage, either before the placement new ran
        // at all or part-way through it. There is nothing to release, no
        // destructor to run and no semaphore to clean up -- and it must never
        // reach releaseMessage, because decrementing from zero trips the DEBUG
        // double-release assert on a legitimate reclaim and, in release, wraps
        // the count so the block could never be freed again.
        if (msg->mRefCount.load(std::memory_order_relaxed) == 0) {
            mMessagePool.deallocate(raw);
            ++raws;
            continue;
        }

        // Otherwise through releaseMessage, not the pool: the block carries a
        // reference count, a destructor and possibly a completion semaphore,
        // and freeing it behind their backs would skip all three.
        releaseMessage(msg);
        ++dropped;
    }

    if (dropped > 0) {
        LOG_INFO("Reclaimed %u message(s) from PID 0x%08X\n",
                (unsigned)dropped, (unsigned)pid);
    }

    // Counted and reported separately. The two mean different things: a
    // reference given back is the ordinary case this exists for, while a
    // never-constructed block means the process was killed inside
    // allocateMessage, between the pool handing out the block and the
    // placement new. That is the rarer and more interesting signal, and
    // folding it into the same number would hide it.
    if (raws > 0) {
        LOG_WARNING("Reclaimed %u unconstructed block(s) from PID 0x%08X\n",
                (unsigned)raws, (unsigned)pid);
    }

    // Never expected: a claim is given back by the same call that gives back
    // the reference it stands for. If this ever fires, something released a
    // message through a different IAppComm than the one it acquired it
    // through, and the owner table can no longer be trusted for that block.
    if (stale > 0) {
        LOG_ERROR("Ignored %u stale claim(s) from PID 0x%08X\n",
                (unsigned)stale, (unsigned)pid);
    }

    return dropped + raws + stale;
}

void MessageManager::retainMessage(SDK::MessageBase* msg)
{
    if (msg == nullptr) {
        return;
    }

    msg->mRefCount.fetch_add(1);
}

bool MessageManager::initCompletionSemaphore(SDK::MessageBase* msg)
{
    if (msg == nullptr) {
        return false;
    }

    // Create dynamic semaphore (FreeRTOS will allocate memory)
    osSemaphoreAttr_t attr = {0};
    attr.name = "MsgCompl";
    // attr.cb_mem = NULL - dynamic allocation
    // attr.cb_size = 0 - dynamic allocation

    msg->mCompletionSemaphore = osSemaphoreNew(1, 0, &attr);

    if (msg->mCompletionSemaphore == nullptr) {
        return false;
    }
    msg->mNeedsResponse = true;

    // Reset explicitly rather than relying on the block having been zeroed.
    // Every message that reaches here today is freshly allocated and
    // allocateRawMemory memsets, so this is currently a no-op.
    //
    // It is not waitCompletion that needs it -- that blocks on the semaphore,
    // which is created with a count of zero, and never reads this flag. The
    // flag matters because isCompleted() is public API: an app that polls
    // rather than waits reads it directly, and a stale true would tell it a
    // response had arrived when none had. Nothing calls isCompleted() in
    // either tree today, which is why this is a no-op and not a bug.
    //
    // The simulator's version of this function already resets it; this is the
    // same divergence in the other direction.
    msg->mCompleted.store(false, std::memory_order_relaxed);

    return true;
}

bool MessageManager::waitCompletion(SDK::MessageBase* msg, uint32_t timeoutMs)
{
    if (msg == nullptr || msg->mCompletionSemaphore == nullptr) {
        return false;
    }

    // Wait on semaphore
    osSemaphoreId_t sem = static_cast<osSemaphoreId_t>(msg->mCompletionSemaphore);
    osStatus_t status = osSemaphoreAcquire(sem, timeoutMs);
    return (status == osOK);
}

void MessageManager::signalCompletion(SDK::MessageBase* msg)
{
    if (msg == nullptr || msg->mCompletionSemaphore == nullptr) {
        return;
    }

    // Set completed flag and release semaphore
    msg->mCompleted.store(true);
    osSemaphoreId_t sem = static_cast<osSemaphoreId_t>(msg->mCompletionSemaphore);
    osSemaphoreRelease(sem);
}

void MessageManager::signalCompletion(SDK::MessageBase* msg, SDK::MessageResult result)
{
    if (msg == nullptr) {
        return;
    }
    msg->setResult(result);
    signalCompletion(msg);
}

void MessageManager::cleanupCompletionSemaphore(SDK::MessageBase* msg)
{
    if (msg == nullptr || msg->mCompletionSemaphore == nullptr) {
        return;
    }

    osSemaphoreId_t sem = static_cast<osSemaphoreId_t>(msg->mCompletionSemaphore);
    osSemaphoreDelete(sem);
    msg->mCompletionSemaphore = nullptr;
}

} // namespace App

