/**
 * @file MessagePool.hpp
 * @brief Universal message pool with multiple size categories
 *
 * This file is part of kernel infrastructure only.
 * Provides efficient memory allocation for messages of different sizes.
 *
 * Pool categories can be extended by modifying POOL_CONFIGS array.
 */

#ifndef __APP_MESSAGE_POOL_HPP
#define __APP_MESSAGE_POOL_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>
#include "cmsis_os2.h"
#include "freertos_mpool.h"

namespace App
{

/**
 * @brief Pool configuration structure
 */
struct PoolConfig {
    size_t blockSize;       // Size of each block in bytes
    size_t blockCount;      // Number of blocks in pool
};

/**
 * @brief Pool configurations for different message sizes
 * @note To add new pool size: add entry to this array and update POOL_COUNT
 *
 * Design guidelines:
 * - Small pools (32-64 bytes): for events and simple requests
 * - Medium pools (128 bytes): for sensor data and complex requests
 * - Large pools (256 bytes): for bulk data transfers
 * - Adjust blockCount based on expected usage patterns
 */
constexpr PoolConfig POOL_CONFIGS[] = {
    {32,  100},  // Pool 0: Small messages (events, simple requests)
    {64,  100},  // Pool 1: Medium-small messages
    {128, 50},   // Pool 2: Medium messages (sensor data)
    {256, 50}    // Pool 3: Large messages (bulk transfers)
};

constexpr size_t POOL_COUNT = sizeof(POOL_CONFIGS) / sizeof(PoolConfig);

/**
 * @brief Maximum message alignment requirement
 */
constexpr size_t MAX_MESSAGE_ALIGN = 8;

/**
 * @brief Universal message pool
 *
 * Manages memory allocation for messages of different sizes.
 * Uses CMSIS-RTOS2 memory pools for efficient allocation.
 *
 * Features:
 * - Multiple size categories in single pool manager
 * - Thread-safe allocation/deallocation
 * - Static memory (no dynamic allocation)
 * - Automatic pool selection based on message size
 * - Statistics tracking (free/used counts)
 *
 * Usage:
 * 1. Create MessagePool singleton in kernel
 * 2. Call allocate(size) to get memory block
 * 3. Use placement new to construct message
 * 4. Call deallocate(ptr) when done
 */
class MessagePool {
public:
    /**
     * @brief Create message pool with all size categories
     */
    MessagePool();

    /**
     * @brief Destroy message pool
     */
    ~MessagePool();

    /**
     * @brief Allocate memory block for message
     * @param size Required size in bytes
     * @param outPoolId Output parameter for pool identifier
     * @param timeout Timeout in milliseconds (0 for non-blocking)
     * @retval Pointer to allocated memory on success
     * @retval nullptr if no suitable pool or allocation failed
     * @note Selects smallest pool that fits requested size
     * @note outPoolId must be saved to know which pool to return to
     */
    void* allocate(size_t size, uint8_t& outPoolId, uint32_t timeout = 0);

    /**
     * @brief Deallocate memory block back to pool
     * @param ptr Pointer to memory block
     * @note Safe to call with nullptr
     * @note Automatically determines correct pool by address
     * @note Logs error if address invalid or deallocation fails
     */
    void deallocate(void* ptr);

    /**
     * @brief Get free block count for specific pool
     * @param poolId Pool identifier (0 to POOL_COUNT-1)
     * @retval Number of free blocks
     */
    size_t getFreeCount(uint8_t poolId) const;

    /**
     * @brief Get used block count for specific pool
     * @param poolId Pool identifier (0 to POOL_COUNT-1)
     * @retval Number of used blocks
     */
    size_t getUsedCount(uint8_t poolId) const;

    /**
     * @brief Get total capacity for specific pool
     * @param poolId Pool identifier (0 to POOL_COUNT-1)
     * @retval Total number of blocks in pool
     */
    size_t getCapacity(uint8_t poolId) const;

    /**
     * @brief Get block size for specific pool
     * @param poolId Pool identifier (0 to POOL_COUNT-1)
     * @retval Block size in bytes
     */
    size_t getBlockSize(uint8_t poolId) const;

    /**
     * @brief Get the maximum block size
     * @retval Block size in bytes
     */
    size_t getMaxBlockSize() const;

    /**
     * @brief Find pool that owns given memory address
     * @param ptr Memory address to check
     * @retval Pool ID if address belongs to a pool (0 to POOL_COUNT-1)
     * @retval 0xFF if address does not belong to any pool
     * @note Thread-safe operation (read-only check)
     */
    uint8_t findPoolByAddress(void* ptr) const;

    /**
     * @brief Get total number of pools
     * @retval Number of pool categories
     */
    static constexpr size_t getPoolCount() { return POOL_COUNT; }

    /**
     * @brief Find suitable pool for given size
     * @param size Required size in bytes
     * @retval Pool ID if suitable pool found
     * @retval 0xFF if no suitable pool exists
     * @note Returns smallest pool that can fit the size
     */
    uint8_t findPoolForSize(size_t size) const;

    /**
     * @brief   Record that @p pid holds a reference it has not yet returned.
     * @param   ptr: Block previously returned by allocate().
     * @param   pid: The process that owes the reference.
     * @retval  true if the claim is recorded, false if both slots are taken.
     *
     * THE INVARIANT, which everything here depends on: a block claimed by P
     * means P holds a reference it still has to give back. Not "was handed to
     * P" -- that is weaker, and a sweep acting on it would drop a reference
     * somebody else still holds.
     *
     * TWO SLOTS, because two processes can owe a reference to one block at the
     * same time: the process that allocated it still owes its own reference
     * after it has sent the message, and the process that received it owes the
     * one it was handed. With a single slot the second claim overwrote the
     * first, which silently made the sender's reference unreclaimable -- the
     * block leaked exactly as it did before any of this existed.
     *
     * Two is a fit to how messages are used, not a guarantee. An app that
     * replies by returning the SAME message object needs three, and nothing in
     * IAppComm forbids that. A third claim is refused rather than displacing
     * either of the first two, so that case degrades to one unreclaimable
     * reference -- a leak -- instead of a use-after-free.
     *
     * Claims are counted, not de-duplicated: one process can legitimately owe
     * the same block twice (it allocated the message AND later received it
     * back), and collapsing those would leave the second reference unrecorded.
     *
     * WHO MAY CALL THIS: only the owning process, on its own thread. Claims are
     * added where a process takes on a reference -- allocation, and the pop in
     * IAppComm::getMessage -- and removed in IAppComm::releaseMessage where it
     * gives one back. A sweep runs only after that thread has been terminated,
     * so the sweep never races the process it is sweeping. Nothing claims on
     * behalf of a process, which is what lets a message sitting in a queue stay
     * unclaimed: the queue's reference belongs to the sender, not the receiver,
     * until the receiver actually takes it.
     */
    bool addOwner(void* ptr, uint32_t pid);

    /**
     * @brief   Note that @p pid has returned its reference.
     * @retval  true if the block was claimed by @p pid and is now clear.
     *
     * Only clears a claim belonging to @p pid, so one process cannot drop
     * another's.
     */
    bool removeOwner(void* ptr, uint32_t pid);

    /**
     * @brief   Hand back the next block claimed by @p pid, clearing that claim.
     * @param   pid:    The process being swept.
     * @param   cursor: Scan position; start at 0 and pass the same variable back.
     * @retval  The block, or nullptr when there are no more.
     *
     * Call only once @p pid's thread has been terminated. That is what makes
     * the scan safe rather than any locking here: the only code that adds or
     * removes @p pid's claims is @p pid itself, so once it is gone the claims
     * it left behind are stable.
     *
     * The claim is still taken with a compare-exchange, because this slot has
     * writers other than @p pid: clearOwners() when the block reaches the free
     * list, and a second sweep of the same PID (reachable when the dispatch
     * thread join times out and ~AppInstance sweeps alongside it). Without it
     * two sweeps could return the same block and the second release would run
     * against memory already on the free list.
     *
     * The claim is cleared as the block is taken, which is also what makes the
     * caller's loop terminate: messages are reference counted, so dropping the
     * dead process's reference may not free the block, and a still-live block
     * must not be handed out twice.
     *
     * A block may be returned twice for two DIFFERENT pids -- once per slot --
     * which is correct: that is two separate references being given back.
     *
     * Deliberately does NOT free anything. Blocks carry a reference count, a
     * virtual destructor and possibly a completion semaphore, so releasing one
     * is MessageManager's job -- see MessageManager::releaseByOwner.
     */
    void* takeOwned(uint32_t pid, size_t& cursor);

    /**
     * @brief   Does the pool currently have this block handed out?
     * @retval  false if the block is on the free list, or not ours at all.
     *
     * Tracked here rather than inferred from the block's contents. A sweep
     * finding a claim has to know whether the block behind it is still
     * allocated before it does anything irreversible, and the block itself
     * cannot tell it: a free block's first word holds the free-list link,
     * which is NULL for whichever block sits at the bottom of the list -- and
     * on a pool that has been fully allocated, that is simply the first block
     * freed afterwards. Reading zero there and concluding "never constructed"
     * would free an already-free block, which the CMSIS pool does not detect:
     * it would push the block on twice, make the list cyclic, and hand the
     * same memory to two owners.
     */
    bool isAllocated(void* ptr) const;

    /**
     * @brief   How many times a claim could not be recorded for want of a slot.
     * @retval  Cumulative since boot.
     *
     * Non-zero means some references have stopped being reclaimable, so the
     * leak this class exists to close is partly back. It also explains an
     * otherwise alarming symptom: a process that owes more references than it
     * has claims will find no claim to give back on its last release.
     */
    uint32_t getSlotExhaustionCount() const {
        return mSlotExhaustionCount.load(std::memory_order_relaxed);
    }

    /// No owner: the block is the kernel's, or is free.
    ///
    /// Not 0, which is a legal PID: PIDs are derived as (pathCRC & 0xFFFFFFF0)
    /// for a service and that +1 for its GUI, so a CRC below 0x10 yields PID 0
    /// and the sweep would silently skip that app. 0xFFFFFFFF cannot be
    /// produced by that derivation, because the mask clears the low nibble.
    static constexpr uint32_t skNoOwner = 0xFFFFFFFFu;

private:
    /**
     * @brief Single pool category
     */
    struct Pool {
        osMemoryPoolId_t memoryPoolId;
        StaticMemPool_t memoryPoolCb;
        size_t blockSize;
        size_t blockCount;
        void* storage;

        Pool()
            : memoryPoolId(nullptr)
            , blockSize(0)
            , blockCount(0)
            , storage(nullptr)
        {}
    };

    Pool mPools[POOL_COUNT];

    /// Total blocks across all pools, and the owner of each.
    ///
    /// A parallel array rather than a per-block header: blocks are fixed size
    /// within a pool and their storage is contiguous, so a block's index is
    /// (addr - storage) / blockSize. That keeps the block layout, alignment
    /// and the CMSIS pool itself untouched.
    static constexpr size_t skTotalBlocks = []() {
        size_t n = 0;
        for (size_t i = 0; i < POOL_COUNT; ++i) {
            n += POOL_CONFIGS[i].blockCount;
        }
        return n;
    }();

    /// Claims per block. Two, so an allocator and a receiver can both owe a
    /// reference to the same block -- see addOwner().
    static constexpr size_t skOwnerSlots = 2;

    /// Atomic because one slot of a block can be claimed or cleared by a live
    /// process while a teardown sweeps the other.
    ///
    /// Relaxed ordering is enough, and the reason is exclusivity rather than
    /// any ordering the atomic provides: once a process's thread is terminated
    /// nothing else reads or writes that process's claims, so the sweep is the
    /// only accessor of both the claim and the block it names. There is no
    /// second party to be ordered against. (Do not read the relaxed load as
    /// establishing anything about the block's contents -- it does not, and a
    /// future caller that acted on state the claim did not hand it would need
    /// more.)
    std::atomic<uint32_t> mOwners[skTotalBlocks * skOwnerSlots];
    size_t   mOwnerBase[POOL_COUNT];   ///< index of pool i's first block

    /// Whether each block is currently handed out. Set as it leaves the pool
    /// and cleared before it goes back, so a sweep can ask instead of guess.
    std::atomic<bool> mAllocated[skTotalBlocks];

    /// Times addOwner() found both slots taken. Cumulative rather than a
    /// one-shot flag so that one app hitting it cannot hide another, and
    /// readable via getSlotExhaustionCount() -- a counter nothing can read
    /// would be worse than no counter, because the rationale would be a lie.
    std::atomic<uint32_t> mSlotExhaustionCount;

    /// Flat index of a block, or skTotalBlocks if the address is not ours.
    size_t ownerIndex(void* ptr) const;

    /// Drop every claim on a block. Used when it reaches the free list, where
    /// a surviving claim would let a later sweep hand out a free block.
    ///
    /// The one place that clears a claim it does not own, with a plain store
    /// rather than a compare-exchange. Safe because it runs only from
    /// deallocate(), i.e. at a reference count of zero: every owner has
    /// already given its reference back, so by the invariant no claim can
    /// still be outstanding and there is nothing to race for.
    void clearOwners(void* ptr);

    /**
     * @brief Storage for all pools
     * @note Aligned storage for each pool category
     */
    struct alignas(MAX_MESSAGE_ALIGN) Storage0 {
        uint8_t data[POOL_CONFIGS[0].blockSize];
    };

    struct alignas(MAX_MESSAGE_ALIGN) Storage1 {
        uint8_t data[POOL_CONFIGS[1].blockSize];
    };

    struct alignas(MAX_MESSAGE_ALIGN) Storage2 {
        uint8_t data[POOL_CONFIGS[2].blockSize];
    };

    struct alignas(MAX_MESSAGE_ALIGN) Storage3 {
        uint8_t data[POOL_CONFIGS[3].blockSize];
    };

    Storage0 mStorage0[POOL_CONFIGS[0].blockCount];
    Storage1 mStorage1[POOL_CONFIGS[1].blockCount];
    Storage2 mStorage2[POOL_CONFIGS[2].blockCount];
    Storage3 mStorage3[POOL_CONFIGS[3].blockCount];

    /// The address-to-index map divides by blockSize, so the C++ array stride
    /// must equal the stride the CMSIS pool hands blocks out at. alignas()
    /// rounds sizeof(StorageN) UP to a multiple of MAX_MESSAGE_ALIGN while the
    /// pool strides by the raw blockSize, so the two agree only while every
    /// blockSize is a multiple of that alignment. They are today (32/64/128/
    /// 256). Add a 36-byte pool -- the size of RequestAppTerminate, so not a
    /// far-fetched tweak -- and C++ would stride by 40 against the pool's 36:
    /// the upper blocks would fall outside the range findPoolByAddress checks
    /// and be silently unclaimable, while the rest aliased onto each other's
    /// entries. Nothing would fail loudly, so it is checked here instead.
    static_assert(POOL_COUNT == 4,
                  "add a StorageN member and an initPool() call for the new pool");
    static_assert(sizeof(Storage0) == POOL_CONFIGS[0].blockSize, "pool 0 stride");
    static_assert(sizeof(Storage1) == POOL_CONFIGS[1].blockSize, "pool 1 stride");
    static_assert(sizeof(Storage2) == POOL_CONFIGS[2].blockSize, "pool 2 stride");
    static_assert(sizeof(Storage3) == POOL_CONFIGS[3].blockSize, "pool 3 stride");

    /**
     * @brief Initialize single pool
     * @param poolId Pool identifier
     * @param blockSize Size of each block
     * @param blockCount Number of blocks
     * @param storage Pointer to storage array
     */
    void initPool(uint8_t poolId, size_t blockSize, size_t blockCount, void* storage);

    // Prevent copying
    MessagePool(const MessagePool&) = delete;
    MessagePool& operator=(const MessagePool&) = delete;
};

} // namespace App

#endif // __APP_MESSAGE_POOL_HPP
