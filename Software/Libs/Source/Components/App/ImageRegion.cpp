/**
 ******************************************************************************
 * @file    ImageRegion.cpp
 * @brief   Downward-growing address region for loaded application images
 ******************************************************************************
 */

#include "App/ImageRegion.hpp"

namespace App {

size_t ImageRegion::roundUp(size_t bytes)
{
    return (bytes + (skAlignment - 1u)) & ~(skAlignment - 1u);
}

void ImageRegion::init(uintptr_t top)
{
    mTop   = top & ~static_cast<uintptr_t>(skAlignment - 1u);
    mCount = 0;
}

uintptr_t ImageRegion::floor() const
{
    // Extents are sorted highest first, so the last one is the lowest. With
    // none resident the floor is the top, which leaves the kernel the whole
    // heap and makes the scheme free until an image actually loads.
    return (mCount == 0) ? mTop : mExtents[mCount - 1].base;
}

size_t ImageRegion::bytesInUse() const
{
    size_t total = 0;
    for (size_t i = 0; i < mCount; i++) {
        total += mExtents[i].size;
    }
    return total;
}

uintptr_t ImageRegion::allocate(size_t bytes, uintptr_t breakAddr)
{
    // The size bound is checked by subtraction, before roundUp adds to it.
    // roundUp computes `bytes + (skAlignment - 1)`, so a size within
    // skAlignment-1 of SIZE_MAX wraps and `want` becomes 0 - whereupon every
    // gap test below passes trivially and the placement returns mTop with a
    // zero-length extent. mTop is one past the region, which is the ISR
    // stack. The old `new (nothrow) uint32_t[]` path refused such a request,
    // so this guard is what keeps that property.
    if ((bytes == 0) || (bytes > (SIZE_MAX - (skAlignment - 1u))) ||
        (mTop == 0) || (mCount >= skMaxImages)) {
        return 0;
    }

    const size_t want = roundUp(bytes);

    // Walk the gaps from the top downward and take the first that fits, which
    // is therefore the highest. Every byte the lowest image sits above is a
    // byte the kernel can still reach, so the first fit from the top is the
    // placement that protects the floor - see the class comment for why best
    // fit would be the wrong instinct here.
    uintptr_t gapTop = mTop;

    for (size_t i = 0; i < mCount; i++) {
        const uintptr_t gapBottom = mExtents[i].base + mExtents[i].size;

        if ((gapTop > gapBottom) && ((gapTop - gapBottom) >= want)) {
            // Placed at the TOP of the gap, not the bottom: the remainder is
            // left low, where it stays adjacent to the next gap down and can
            // still merge with it implicitly.
            const uintptr_t base = gapTop - want;
            insertExtent(base, want, i);
            return base;
        }

        gapTop = mExtents[i].base;
    }

    // The last gap runs from the lowest extent down to the break. This is the
    // only place the kernel's position is consulted, and it is consulted
    // rather than remembered so a break that moved since the last call cannot
    // be acted on stale.
    if ((gapTop > breakAddr) && ((gapTop - breakAddr) >= want)) {
        const uintptr_t base = gapTop - want;
        insertExtent(base, want, mCount);
        return base;
    }

    return 0;
}

bool ImageRegion::release(uintptr_t base)
{
    if (base == 0) {
        return true;
    }

    for (size_t i = 0; i < mCount; i++) {
        if (mExtents[i].base != base) {
            continue;
        }

        for (size_t j = i; (j + 1) < mCount; j++) {
            mExtents[j] = mExtents[j + 1];
        }
        mCount--;

        // Nothing to coalesce: free space is whatever is not in this list, so
        // two neighbours releasing leave one gap without anything merging
        // them.
        return true;
    }

    // Not ours. Silent success would hide a double release or a stray
    // pointer, and the region has no way to detect either later.
    return false;
}

size_t ImageRegion::largestFreeBlock(uintptr_t breakAddr) const
{
    size_t    best   = 0;
    uintptr_t gapTop = mTop;

    for (size_t i = 0; i < mCount; i++) {
        const uintptr_t gapBottom = mExtents[i].base + mExtents[i].size;
        if (gapTop > gapBottom) {
            const size_t gap = static_cast<size_t>(gapTop - gapBottom);
            if (gap > best) {
                best = gap;
            }
        }
        gapTop = mExtents[i].base;
    }

    if (gapTop > breakAddr) {
        const size_t gap = static_cast<size_t>(gapTop - breakAddr);
        if (gap > best) {
            best = gap;
        }
    }

    return best;
}

void ImageRegion::insertExtent(uintptr_t base, size_t size, size_t at)
{
    for (size_t j = mCount; j > at; j--) {
        mExtents[j] = mExtents[j - 1];
    }
    mExtents[at].base = base;
    mExtents[at].size = size;
    mCount++;
}

} // namespace App
