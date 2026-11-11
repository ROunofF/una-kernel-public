/**
 ******************************************************************************
 * @file    AppMemAllocator.hpp
 * @brief   A memory allocator that tracks allocated memory blocks for the
 *          external user application.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __APP_MEM_ALLOCATOR_HPP
#define __APP_MEM_ALLOCATOR_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>

#include "SDK/Interfaces/IAppMemory.hpp"


namespace App
{

/**
 * @brief   A memory allocator that tracks allocated memory blocks.
 *
 * This class provides custom memory allocation and deallocation with tracking.
 * If any allocated memory block is not freed by the user, the class ensures it
 * is released upon destruction.
 *
 * Every allocation is counted against a per-process heap quota so a leaky or
 * greedy app is denied (malloc returns nullptr) before it can exhaust the
 * shared newlib heap and take kernel allocations down with it. The quota
 * covers the app's runtime heap only; the code/data/bss/stack segments are
 * allocated by the Loader and are not routed through this allocator.
 */
class MemAllocator : public SDK::Interface::IAppMemory {

public:

    /**
     * @brief   Per-process heap quota (requested bytes outstanding).
     *
     * Sizing: the shared newlib heap is ~1.53 MB and also
     * feeds the kernel and the Loader's app code/data/bss/stack segments.
     * Most shipping-app heap use is tens of KB per process (KB-scale
     * settings/summary JSON buffers, laps vectors, 3.6 KB TouchGFX canvas
     * buffers). The sizing case is SDK TrackMapBuilder: the activity apps
     * append one 8-byte GpsPoint per ~10 m travelled with no cap until
     * activity-end decimation, and each std::vector doubling charges the old
     * block plus the new request while the copy is pending. Growing from
     * 11,520 to 23,040 points costs 92,160 outstanding + 184,320 requested
     * = ~276 KB, so a 256 KiB quota would deny a legitimate ride at ~115 km
     * -- under a century ride. At 512 KiB the first denied grow is from
     * 23,040 to 46,080 points (184,320 + 368,640 = ~553 KB), i.e. beyond
     * ~230 km of track: past any plausible single activity on one battery
     * charge. The two-process worst case (service + GUI both at quota) is
     * 1 MB of the 1.53 MB heap; acceptable because only one activity app
     * runs at a time in practice and kernel allocation failures abort loudly
     * rather than silently corrupting.
     */
    static constexpr size_t skHeapQuotaBytes = 512U * 1024U;

    /**
     * @brief   Constructor.
     * @param   name: Process name used in log messages (not copied; must
     *          outlive the allocator).
     * @param   quotaBytes: Per-process heap quota. Defaults to
     *          @ref skHeapQuotaBytes; the parameter is the seam for a future
     *          per-app manifest override.
     */
    MemAllocator(const char *name, size_t quotaBytes = skHeapQuotaBytes);

    /**
     * @brief   Destructor.
     *
     * Frees any remaining allocated memory blocks to prevent memory leaks.
     */
    virtual ~MemAllocator();

    /**
     * @brief   Frees any remaining allocated memory blocks to prevent memory leaks.
     */
    void freeAll();

    /**
     * @brief   Allocates a block of memory.
     * @param   size: The size of the memory block to allocate in bytes.
     * @return  A pointer to the allocated memory block, or nullptr if
     *          allocation failed.
     */
    virtual void* malloc(size_t size) override;

    /**
     * @brief   Frees a previously allocated memory block.
     * @param   ptr: A pointer to the memory block to free.
     */
    virtual void free(void *ptr) override;

    /**
     * @brief Reallocates a memory block
     *
     * @param ptr  Previously allocated pointer or nullptr.
     * @param size New block size in bytes.
     */
    virtual void* realloc(void* ptr, size_t size) override;

    /**
     * @brief   Requested bytes currently outstanding (allocated, not freed).
     */
    size_t getOutstandingBytes() const { return mOutstanding; }

    /**
     * @brief   Number of allocations denied by the quota since the last
     *          freeAll() (i.e. since the current app run started).
     */
    uint32_t getDenialCount() const { return mDenials; }

    /**
     * @brief   The quota this allocator enforces, in bytes.
     */
    size_t getQuotaBytes() const { return mQuotaBytes; }

private:

    /// Application name
    const char* mName;

    /// Initial number of tracked memory blocks.
    static constexpr uint32_t skBlocksCount = 32;

    /// After the first denial (LOG_ERROR), log only every Nth subsequent
    /// denial (LOG_WARNING): the deny site sits on the app's malloc path and
    /// can be hit in a tight retry loop.
    static constexpr uint32_t skDenialLogPeriod = 100;

    /// Per-process heap quota in bytes.
    size_t mQuotaBytes;

    /// Requested bytes currently outstanding against the quota.
    size_t mOutstanding = 0;

    /// Quota denials since the last freeAll().
    uint32_t mDenials = 0;

    /// This instance's net contribution to the global HeapTelemetry
    /// app-allocation gauges (bytes / blocks published and not yet
    /// retracted). Moved in lock-step with every global gauge update so
    /// freeAll() can retract this instance's whole share in one step without
    /// touching the other allocators' shares -- see the freeAll()
    /// implementation, which also documents the accepted best-effort drift.
    uint32_t mGaugeBytes = 0;
    uint32_t mGaugeBlocks = 0;

    /// Vector storing pointers to allocated memory blocks with reserved capacity.
    std::vector<void*> mBlocks;

    /// Store the size of each block
    std::unordered_map<void*, size_t> mSizes;

    /// Count a quota denial and log it (first at ERROR, then rate-limited).
    void recordDenial(size_t size);

    /// Test seam: lets the host unit tests forge the torn state a
    /// force-terminate (osThreadTerminate) can leave behind -- a block
    /// tracked in mBlocks/mSizes whose accounting updates were skipped.
    friend struct MemAllocatorTestPeer;
};

} // namespace App

#endif // __APP_MEM_ALLOCATOR_HPP
