/**
 * @file MessagePool.cpp
 * @brief Implementation of universal message pool
 */

#include "App/MessagePool.hpp"

#define LOG_MODULE_PRX      "App.MessagePool"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

namespace App
{

MessagePool::MessagePool()
{
    // Owner table first: every block starts unowned, and each pool's blocks
    // occupy a contiguous run of the flat array so an address maps to an index
    // with one subtraction and one divide.
    size_t base = 0;
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        mOwnerBase[i] = base;
        base += POOL_CONFIGS[i].blockCount;
    }
    for (size_t i = 0; i < (skTotalBlocks * skOwnerSlots); ++i) {
        mOwners[i].store(skNoOwner, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < skTotalBlocks; ++i) {
        mAllocated[i].store(false, std::memory_order_relaxed);
    }
    mSlotExhaustionCount.store(0, std::memory_order_relaxed);

    // Initialize all pools according to POOL_CONFIGS
    // Pool 0:
    initPool(0, POOL_CONFIGS[0].blockSize, POOL_CONFIGS[0].blockCount, mStorage0);

    // Pool 1:
    initPool(1, POOL_CONFIGS[1].blockSize, POOL_CONFIGS[1].blockCount, mStorage1);

    // Pool 2:
    initPool(2, POOL_CONFIGS[2].blockSize, POOL_CONFIGS[2].blockCount, mStorage2);

    // Pool 3:
    initPool(3, POOL_CONFIGS[3].blockSize, POOL_CONFIGS[3].blockCount, mStorage3);
}

MessagePool::~MessagePool()
{
    // Delete all memory pools
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        if (mPools[i].memoryPoolId != nullptr) {
            osMemoryPoolDelete(mPools[i].memoryPoolId);
            mPools[i].memoryPoolId = nullptr;
        }
    }
}

void MessagePool::initPool(uint8_t poolId, size_t blockSize, size_t blockCount, void* storage)
{
    configASSERT(poolId < POOL_COUNT);
    configASSERT(storage != nullptr);

    Pool& pool = mPools[poolId];
    pool.blockSize = blockSize;
    pool.blockCount = blockCount;
    pool.storage = storage;

    // Create CMSIS-RTOS2 memory pool
    osMemoryPoolAttr_t attr = {0};

    // Name for debugging
    static const char* poolNames[POOL_COUNT] = {
        "MsgPool0",
        "MsgPool1",
        "MsgPool2",
        "MsgPool3"
    };
    attr.name = poolNames[poolId];

    attr.attr_bits = 0;
    attr.cb_mem = &pool.memoryPoolCb;
    attr.cb_size = sizeof(StaticMemPool_t);
    attr.mp_mem = storage;
    attr.mp_size = blockSize * blockCount;

    pool.memoryPoolId = osMemoryPoolNew(blockCount, blockSize, &attr);

    // Assert if pool creation failed
    configASSERT(pool.memoryPoolId != nullptr);
}

void* MessagePool::allocate(size_t size, uint8_t& outPoolId, uint32_t timeout)
{
    // Find suitable pool
    outPoolId = findPoolForSize(size);

    if (outPoolId >= POOL_COUNT) {
        // No suitable pool found
        return nullptr;
    }

    // Allocate from selected pool
    Pool& pool = mPools[outPoolId];
    void* ptr = osMemoryPoolAlloc(pool.memoryPoolId, timeout);

    // Before the caller can do anything with it, so a sweep that sees a claim
    // on this block also sees that the block is live.
    if (ptr != nullptr) {
        const size_t idx = ownerIndex(ptr);
        if (idx < skTotalBlocks) {
            mAllocated[idx].store(true, std::memory_order_relaxed);
        }
    }

    LOG_DEBUG("0x%08X. ID %u, Free %u\n", (uint32_t)ptr, outPoolId, getFreeCount(outPoolId));
    return ptr;
}

size_t MessagePool::ownerIndex(void* ptr) const
{
    const uint8_t poolId = findPoolByAddress(ptr);
    if (poolId >= POOL_COUNT) {
        return skTotalBlocks;
    }

    const Pool& pool = mPools[poolId];
    const uintptr_t off = reinterpret_cast<uintptr_t>(ptr)
                        - reinterpret_cast<uintptr_t>(pool.storage);
    const size_t idx = mOwnerBase[poolId] + (off / pool.blockSize);

    return (idx < skTotalBlocks) ? idx : skTotalBlocks;
}

bool MessagePool::addOwner(void* ptr, uint32_t pid)
{
    if (pid == skNoOwner) {
        return false;
    }

    const size_t idx = ownerIndex(ptr);
    if (idx >= skTotalBlocks) {
        return false;
    }

    const size_t base = idx * skOwnerSlots;

    // No de-duplication: one claim per reference, even when both belong to the
    // same process. A process CAN owe a block twice -- it allocates a message
    // (one reference) and later receives that same message back from its peer
    // (a second one) -- and collapsing those onto a single claim would leave
    // the second reference with nothing recording it, so a sweep would drop
    // only one and the block would still be lost. That is the very leak this
    // exists to close, and it would be silent.
    //
    // This is safe only because claims and references are one-to-one at every
    // site: allocateMessage takes a reference and adds a claim, the pop in
    // getMessage takes one and adds one, releaseMessage gives one back and
    // removes one. Nothing claims without taking a reference, so a sweep that
    // returns a block once per claim drops exactly the references owed.
    for (size_t s = 0; s < skOwnerSlots; ++s) {
        uint32_t expected = skNoOwner;
        if (mOwners[base + s].compare_exchange_strong(expected, pid,
                                                      std::memory_order_relaxed)) {
            return true;
        }
    }

    // Both slots already spoken for, so this one reference is not
    // reclaimable: a leak, exactly the pre-change behaviour, never corruption.
    //
    // Two owners is what current traffic produces, but nothing ENFORCES it.
    // An app that replies by sending the same message object back gets three:
    // the allocator claims it, the peer claims it on receipt, and the
    // allocator claims it again when it receives its own object back. Nothing
    // in IAppComm forbids that, so treat two slots as a fit to how messages
    // are used today rather than as a guarantee.
    //
    // Logged once per boot. If an app does hit this it hits it on every
    // message, and this is an ERROR-level path that is compiled in.
    const uint32_t seen = mSlotExhaustionCount.fetch_add(1, std::memory_order_relaxed);
    if (seen == 0) {
        LOG_ERROR("No owner slot for block 0x%08X, PID 0x%08X (further ones counted only)\n",
                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr)), pid);
    }
    return false;
}

bool MessagePool::isAllocated(void* ptr) const
{
    const size_t idx = ownerIndex(ptr);
    if (idx >= skTotalBlocks) {
        return false;
    }
    return mAllocated[idx].load(std::memory_order_relaxed);
}

bool MessagePool::removeOwner(void* ptr, uint32_t pid)
{
    if (pid == skNoOwner) {
        return false;
    }

    const size_t idx = ownerIndex(ptr);
    if (idx >= skTotalBlocks) {
        return false;
    }

    const size_t base = idx * skOwnerSlots;

    // Only our own claim: a compare-exchange rather than a store so that one
    // process cannot clear another's, and so a concurrent sweep of the other
    // slot sees a whole value either way.
    for (size_t s = 0; s < skOwnerSlots; ++s) {
        uint32_t expected = pid;
        if (mOwners[base + s].compare_exchange_strong(expected, skNoOwner,
                                                      std::memory_order_relaxed)) {
            return true;
        }
    }

    return false;
}

void MessagePool::clearOwners(void* ptr)
{
    const size_t idx = ownerIndex(ptr);
    if (idx >= skTotalBlocks) {
        return;
    }

    const size_t base = idx * skOwnerSlots;
    for (size_t s = 0; s < skOwnerSlots; ++s) {
        mOwners[base + s].store(skNoOwner, std::memory_order_relaxed);
    }
}

void* MessagePool::takeOwned(uint32_t pid, size_t& cursor)
{
    if (pid == skNoOwner) {
        return nullptr;   // the kernel's own blocks are never swept
    }

    // The cursor walks slots, not blocks: a block can carry a claim in either
    // slot, and both must be reachable.
    constexpr size_t skTotalSlots = skTotalBlocks * skOwnerSlots;

    for (; cursor < skTotalSlots; ++cursor) {
        // Claim and take in one step, rather than test and then take.
        //
        // On the paths that exist today no second writer to this slot is
        // reachable: only the owner touches its own claims, the owner is dead,
        // and clearOwners() runs only at a reference count of zero, which this
        // block cannot be at while it still carries a claim. So this could be
        // a load and a store.
        //
        // It is a compare-exchange because of the cost asymmetry, not because
        // of a known race. If a second writer ever does appear -- a second
        // sweep of one PID, a future caller of clearOwners -- test-then-take
        // hands the same block back twice, and the second release puts it on
        // the free list a second time. The pool does not detect that: the
        // block is then handed to two owners at once. One CAS on a teardown
        // path is not worth trading for that.
        uint32_t expected = pid;
        if (!mOwners[cursor].compare_exchange_strong(expected, skNoOwner,
                                                     std::memory_order_relaxed)) {
            continue;
        }

        // Which pool this block falls in, and where in it.
        const size_t blockIdx = cursor / skOwnerSlots;

        size_t p = 0;
        while ((p + 1u) < POOL_COUNT && blockIdx >= mOwnerBase[p + 1u]) {
            ++p;
        }

        const Pool& pool = mPools[p];
        const size_t b = blockIdx - mOwnerBase[p];

        ++cursor;

        return reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(pool.storage) + (b * pool.blockSize));
    }

    return nullptr;
}

void MessagePool::deallocate(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }

    // Belt and braces. Both claims should already be gone -- each is cleared
    // when its owner returns its reference -- but a block reaching the free
    // list must never carry one, or the next sweep would hand out a free
    // block.
    clearOwners(ptr);

    // Marked free BEFORE it goes back, so there is no window in which the
    // pool has taken it but a sweep would still be told it is live.
    const size_t idx = ownerIndex(ptr);
    if (idx < skTotalBlocks) {
        mAllocated[idx].store(false, std::memory_order_relaxed);
    }

    // Find which pool owns this address
    uint8_t poolId = findPoolByAddress(ptr);

    if (poolId >= POOL_COUNT) {
        // Invalid address - not from any pool
        // LOG_ERROR("Invalid address for deallocation: %p (not from any pool)\n", ptr);
        return;
    }

    LOG_DEBUG("0x%08X\n", (uint32_t)ptr);
    Pool& pool = mPools[poolId];
    osStatus_t status = osMemoryPoolFree(pool.memoryPoolId, ptr);

    if (status != osOK) {
        // Free failed - log error but don't crash
        // This can happen if memory corruption or double-free
        // LOG_ERROR("Memory pool free failed: poolId=%u, ptr=%p, status=%d\n",
        //           poolId, ptr, status);
    }
}

uint8_t MessagePool::findPoolForSize(size_t size) const
{
    // Find smallest pool that can fit requested size
    for (uint8_t i = 0; i < POOL_COUNT; ++i) {
        if (mPools[i].blockSize >= size) {
            return i;
        }
    }

    // No suitable pool found
    return 0xFF;
}

size_t MessagePool::getFreeCount(uint8_t poolId) const
{
    if (poolId >= POOL_COUNT) {
        return 0;
    }

    return osMemoryPoolGetSpace(mPools[poolId].memoryPoolId);
}

size_t MessagePool::getUsedCount(uint8_t poolId) const
{
    if (poolId >= POOL_COUNT) {
        return 0;
    }

    return osMemoryPoolGetCount(mPools[poolId].memoryPoolId);
}

size_t MessagePool::getCapacity(uint8_t poolId) const
{
    if (poolId >= POOL_COUNT) {
        return 0;
    }

    return osMemoryPoolGetCapacity(mPools[poolId].memoryPoolId);
}

size_t MessagePool::getBlockSize(uint8_t poolId) const
{
    if (poolId >= POOL_COUNT) {
        return 0;
    }

    return mPools[poolId].blockSize;
}

size_t MessagePool::getMaxBlockSize() const
{
    return mPools[POOL_COUNT - 1].blockSize;
}

uint8_t MessagePool::findPoolByAddress(void* ptr) const
{
    if (ptr == nullptr) {
        return 0xFF;
    }

    // Check each pool to see if address falls within its storage range
    for (uint8_t i = 0; i < POOL_COUNT; ++i) {
        const Pool& pool = mPools[i];

        // Calculate start and end addresses of pool storage
        uintptr_t storageStart = reinterpret_cast<uintptr_t>(pool.storage);
        uintptr_t storageEnd = storageStart + (pool.blockSize * pool.blockCount);
        uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);

        // Check if pointer falls within this pool's range
        if (ptrAddr >= storageStart && ptrAddr < storageEnd) {
            return i;
        }
    }

    // Address does not belong to any pool
    return 0xFF;
}

} // namespace App
