/**
 ******************************************************************************
 * @file    MemAllocator.cpp
 * @brief   A memory allocator that tracks allocated memory blocks for the
 *          external user application.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstring>

#include "App/MemAllocator.hpp"
#include "Shared/HeapTelemetry.hpp"

#define LOG_MODULE_PRX      "MemAllocator"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace App
{

MemAllocator::MemAllocator(const char *name, size_t quotaBytes)
    : mName(name), mQuotaBytes(quotaBytes), mBlocks{}
{
    mBlocks.reserve(skBlocksCount);
}

MemAllocator::~MemAllocator()
{
    freeAll();
}

void* MemAllocator::malloc(size_t size)
{
    // Per-process heap quota: deny before touching the shared newlib heap so a
    // leaky or greedy app degrades alone instead of exhausting the heap the
    // kernel allocates from. Accounting is on requested bytes (the allocator's
    // per-block overhead is not charged). mOutstanding never exceeds
    // mQuotaBytes, so the subtraction cannot underflow and the comparison is
    // immune to (mOutstanding + size) overflow.
    if (size > mQuotaBytes - mOutstanding) {
        recordDenial(size);
        return nullptr;
    }

    // looking for the first empty slot
    auto it = std::find(mBlocks.begin(), mBlocks.end(), nullptr);
    void* ptr = nullptr;

    if (it != mBlocks.end()) {
        *it = ::malloc(size);
        ptr = *it;
        LOG_DEBUG("[%s] reused block 0x%08lX, size %u\n", mName, reinterpret_cast<uintptr_t>(ptr), size);
    } else {
        ptr = ::malloc(size);
        if (ptr) {
            mBlocks.push_back(ptr);
            LOG_DEBUG("[%s] new block 0x%08lX, size %u\n", mName, reinterpret_cast<uintptr_t>(ptr), size);
        }
    }

    if (ptr) {
        mSizes[ptr] = size; // save size
        mOutstanding += size;
        // All-apps outstanding-allocation gauges for the heap monitor, plus
        // this instance's private mirror of what it has published to them.
        // These are best-effort diagnostic telemetry, deliberately lock-free:
        // unlike the exact, enforcement-critical mOutstanding above, a gauge
        // that is off by a block is a cosmetic attribution error, not a
        // correctness bug, so the per-malloc scheduler lock a torn update
        // would need is not worth its cost. The mirror moves in lock-step with
        // the global gauge here so freeAll() can retract exactly what this
        // instance published (see freeAll()); the only residual is a ~1
        // -instruction window between these two adds -- see the note in
        // freeAll() for why that drift is bounded and non-accumulating.
        //
        // Add to the global *before* the mirror here (and free() subtracts
        // from the mirror before the global): both orders leave the global >=
        // this instance's mirror after any torn store, so freeAll()'s
        // wholesale mirror retraction can only ever drift the global high,
        // never unsigned-wrap it low.
        HeapTelemetry::AppAllocBytes().fetch_add(static_cast<uint32_t>(size), std::memory_order_relaxed);
        mGaugeBytes += static_cast<uint32_t>(size);
        HeapTelemetry::AppAllocBlocks().fetch_add(1, std::memory_order_relaxed);
        mGaugeBlocks += 1U;
    }

    return ptr;
}

void MemAllocator::recordDenial(size_t size)
{
    (void)size; // used only by logging, which compiles out in host builds

    ++mDenials;
    if (mDenials == 1U) {
        LOG_ERROR("[%s] heap quota exceeded: request %u, outstanding %u of %u -- denied\n",
                  mName, static_cast<unsigned>(size),
                  static_cast<unsigned>(mOutstanding), static_cast<unsigned>(mQuotaBytes));
    } else if ((mDenials % skDenialLogPeriod) == 0U) {
        LOG_WARNING("[%s] heap quota still exceeded: request %u, outstanding %u of %u, %lu denials\n",
                    mName, static_cast<unsigned>(size),
                    static_cast<unsigned>(mOutstanding), static_cast<unsigned>(mQuotaBytes),
                    static_cast<unsigned long>(mDenials));
    }
}

void* MemAllocator::realloc(void *ptr, size_t size)
{
    if (ptr == nullptr) {
        return malloc(size);
    }

    if (size == 0U) {
        free(ptr);
        return nullptr;
    }

    // Deliberately conservative at the quota edge: the old and the new block
    // are both charged while the copy is pending, so a grow that would fit
    // after the free can still be denied. Acceptable for a protective limit.
    void* newPtr = malloc(size);
    if (!newPtr) {
        LOG_DEBUG("[%s] FAILED for 0x%08lX (size %u)\n", mName, reinterpret_cast<uintptr_t>(ptr), size);
        return nullptr;
    }

    // Safe copying: take the minimum of the old and new size
    size_t oldSize = mSizes[ptr];
    memcpy(newPtr, ptr, std::min(oldSize, size));

    const uintptr_t old_addr = reinterpret_cast<uintptr_t>(ptr);
    free(ptr);

    LOG_DEBUG("[%s] 0x%08lX -> 0x%08lX (size %u)\n", mName, old_addr, reinterpret_cast<uintptr_t>(newPtr), size);
    (void)old_addr;

    return newPtr;
}

void MemAllocator::free(void *ptr)
{
    if (!ptr) return;

    auto it = std::find(mBlocks.begin(), mBlocks.end(), ptr);
    if (it != mBlocks.end() && *it != nullptr) {
        LOG_DEBUG("[%s] free 0x%08lX, size %u\n", mName, reinterpret_cast<uintptr_t>(*it), mSizes[*it]);
        mOutstanding -= mSizes[*it];
        // Retract the gauge and the mirror -- the lock-free counterpart to the
        // publication in malloc(). Order matters: malloc() adds to the global
        // gauge *before* the mirror, so free() must subtract from the mirror
        // *before* the global (mirror-first on the way down). That keeps the
        // global >= this instance's mirror after a force-terminate lands
        // between the two stores of either pair, so freeAll()'s wholesale
        // mirror retraction can only ever drift the global high -- it can
        // never over-retract and unsigned-wrap it below the other allocators'
        // shares. (This runs only on the owning app thread, for a block that
        // thread's malloc() fully published; a terminate in malloc()'s window
        // kills the thread, so no free() ever follows on such a block.)
        const uint32_t published = static_cast<uint32_t>(mSizes[*it]);
        mGaugeBytes -= published;
        HeapTelemetry::AppAllocBytes().fetch_sub(published, std::memory_order_relaxed);
        mGaugeBlocks -= 1U;
        HeapTelemetry::AppAllocBlocks().fetch_sub(1, std::memory_order_relaxed);
        ::free(*it);
        mSizes.erase(*it); // remove from the size map
        *it = nullptr; // reset the slot
    } else {
        // A pointer this allocator did not hand out -- typically a block that
        // was allocated in the sibling process (service vs GUI) of the same
        // app. It cannot be freed here (it is tracked by the sibling's
        // allocator; freeing it raw would double-free at the sibling's
        // freeAll()), and delegating to the sibling is not safe without
        // locking both allocators: each instance is only ever touched by its
        // own process thread today, so neither has any internal locking. The
        // block stays charged to its owner until that process exits.
        LOG_WARNING("[%s] free external 0x%08lX\n", mName, reinterpret_cast<uintptr_t>(ptr));
        //::free(ptr); // block outside the vector
    }
}

void MemAllocator::freeAll()
{
    for (void*& block : mBlocks) {
        if (block) {
            LOG_DEBUG("[%s] freeAll 0x%08lX, size %u\n", mName, reinterpret_cast<uintptr_t>(block), mSizes[block]);
            mOutstanding -= mSizes[block];
            ::free(block);
            mSizes.erase(block);
            block = nullptr;
        }
    }

    // Retract this instance's whole gauge contribution via its private mirror
    // in one step, rather than per freed block. A per-block subtraction would
    // reintroduce the force-terminate hazard: osThreadTerminate can leave a
    // block tracked in mSizes whose gauge publication never ran, and
    // subtracting mSizes[block] for it would remove bytes that were never
    // added -- drifting the global gauges low permanently, since they
    // aggregate every allocator and persist across app runs. The mirror only
    // ever grew alongside the global gauge (malloc()), so it holds this
    // instance's net published contribution and subtracting it returns the
    // gauges to their pre-run baseline; zeroing it here also stops any drift
    // from compounding into the next run. A hard reset like mOutstanding's
    // below is not an option -- it would wipe out the other allocators'
    // shares of these all-apps gauges.
    //
    // Residual (accepted): the gauges are best-effort telemetry, not the
    // exact, enforcement-critical mOutstanding. Because the malloc() and
    // free() gauge updates are lock-free, a force-terminate landing in the
    // ~1-instruction window between the global fetch_add and the mirror += in
    // malloc() leaves the global one block ahead of the mirror; freeAll()
    // then under-retracts that block, drifting the global slightly high. The
    // drift is bounded per event (at most one block's bytes) and never wraps
    // (the ordering keeps global >= mirror, so freeAll() cannot over-retract),
    // but it does NOT self-heal: the mirror-zero here only stops the bias
    // *accumulating across runs of this one instance*; the global keeps that
    // bias until reboot, and each force-terminate event can add another block
    // of it. Accepted for a diagnostic gauge -- not worth a scheduler lock on
    // every malloc/free.
    HeapTelemetry::AppAllocBytes().fetch_sub(mGaugeBytes, std::memory_order_relaxed);
    HeapTelemetry::AppAllocBlocks().fetch_sub(mGaugeBlocks, std::memory_order_relaxed);
    mGaugeBytes = 0;
    mGaugeBlocks = 0;

    // Hard-reset instead of trusting the per-block subtraction to land on
    // zero: a force-terminate (osThreadTerminate) can kill the app thread
    // between malloc's mSizes insert and its mOutstanding += update, after
    // which the subtraction above underflows and wraps mOutstanding huge --
    // permanently disabling the quota for this process (the allocator
    // persists across app runs). freeAll() frees every tracked block, so
    // zero is true by construction.
    mOutstanding = 0;

    // A fresh app run starts with fresh quota accounting: the next denial (if
    // any) is again logged at ERROR.
    mDenials = 0;
}

} // namespace App

