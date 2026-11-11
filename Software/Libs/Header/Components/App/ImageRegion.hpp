/**
 ******************************************************************************
 * @file    ImageRegion.hpp
 * @brief   Downward-growing address region for loaded application images
 ******************************************************************************
 */

#ifndef IMAGE_REGION_HPP
#define IMAGE_REGION_HPP

#include <cstddef>
#include <cstdint>

namespace App {

/**
 * @brief   Hands out address ranges for app images, from the top of RAM down.
 *
 * @details
 * The other half of the two-ended heap. Images are placed
 * downward from a fixed top; the kernel's dlmalloc arena grows upward from the
 * break. The two never interleave, so a kernel allocation can no longer land
 * above an image and fence it, and freeing an image can no longer leave a hole
 * in the middle of the kernel arena.
 *
 * **This class manages addresses, not storage.** It never reads or writes the
 * memory it hands out, and it holds no free list inside that memory - which is
 * what lets the whole thing be tested on a host against arbitrary addresses,
 * and what keeps it from needing an allocator of its own.
 *
 * ### Placement: highest address that fits, always
 *
 * The kernel's ceiling IS the lowest resident image's base, so every byte the
 * lowest image sits above is a byte the kernel can still have. Placement
 * therefore takes the HIGHEST gap an image fits in, packing images against the
 * top and leaving the low end of the region free for as long as possible.
 *
 * Best fit would be wrong here for the same reason. Dropping a small image
 * into a small low gap wastes less space inside the region but drags the floor
 * down, and the floor is the thing being protected. This deliberately wastes a
 * little to keep the floor high.
 *
 * ### Free space is implied, so coalescing is not a step
 *
 * Only the occupied extents are recorded, kept sorted by address. Free space
 * is whatever lies between them, which means two neighbouring images releasing
 * leave one gap rather than two that need merging. There is no free list to
 * coalesce, and no way for it to get out of step with reality.
 */
class ImageRegion {
public:
    /// Most images resident at once. A running app holds two (service and
    /// GUI), a watch face one, and glances one each; the window has been as
    /// wide as three faces plus an activity app plus six glances. Sixteen is
    /// past anything observed, and a request beyond it is refused rather than
    /// overrunning - the caller already handles a refusal, because an image
    /// that will not fit is an ordinary outcome here.
    static constexpr size_t skMaxImages = 16;

    ImageRegion() = default;

    /**
     * @brief   Set the region's top. Call once, before any allocation.
     * @param   top  One past the highest usable address, i.e. the region
     *               occupies addresses below this. On the device it is
     *               `__HeapLimit - ISR_STACK_LENGTH_BYTES`, so the ISR stack
     *               is never inside the region.
     */
    void init(uintptr_t top);

    /**
     * @brief   Reserve @p bytes at the highest address that fits.
     *
     * @param   bytes      Size wanted. Rounded up to skAlignment.
     * @param   breakAddr  The kernel's current break. Nothing is placed below
     *                     it: passing it in rather than reading it keeps this
     *                     class free of any dependency on the heap, and lets
     *                     the caller take the decision inside whatever
     *                     exclusion it already holds.
     * @return  Base address of the reservation, or 0 if it will not fit.
     *
     * @note    Returns 0 rather than asserting. A refusal is the honest
     *          outcome of a full heap and the loader already reports it.
     */
    uintptr_t allocate(size_t bytes, uintptr_t breakAddr);

    /**
     * @brief   Give a reservation back.
     * @param   base  A value previously returned by allocate(). 0 is ignored.
     * @return  false if @p base was not a live reservation - a double release
     *          or a stray pointer, which the caller should treat as a bug
     *          rather than ignore.
     */
    bool release(uintptr_t base);

    /**
     * @brief   Lowest address in use, or the region top when nothing is
     *          resident.
     *
     * This is what `_sbrk_r` must not grow past. When the region is empty it
     * returns the top, so the kernel keeps the whole heap exactly as it does
     * today and the scheme costs nothing until an image is actually loaded.
     */
    uintptr_t floor() const;

    /// Reservations currently held.
    size_t count() const { return mCount; }

    /// Total bytes reserved, excluding any gaps between them.
    size_t bytesInUse() const;

    /// Largest request that would succeed right now against @p breakAddr.
    /// For reporting a refusal usefully, and for tests.
    size_t largestFreeBlock(uintptr_t breakAddr) const;

private:
    /// Images are whole numbers of words and dlmalloc-style alignment costs
    /// nothing at this size, so every reservation is 8-byte aligned.
    static constexpr size_t skAlignment = 8;

    struct Extent {
        uintptr_t base;
        size_t    size;
    };

    static size_t roundUp(size_t bytes);

    /// Insert an extent at index @p at, keeping the array sorted by base
    /// address descending. The caller has already established the position.
    void insertExtent(uintptr_t base, size_t size, size_t at);

    /// Occupied extents, sorted by base address DESCENDING - highest first,
    /// which is also the order placement walks them in.
    Extent    mExtents[skMaxImages] {};
    size_t    mCount { 0 };
    uintptr_t mTop   { 0 };
};

} // namespace App

#endif // IMAGE_REGION_HPP
