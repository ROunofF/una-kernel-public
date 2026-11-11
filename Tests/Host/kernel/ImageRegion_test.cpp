/**
 * Tests for App::ImageRegion - the image half of the two-ended heap.
 *
 * The class manages addresses and never touches the memory it hands out, so
 * these run against arbitrary address values with no allocation at all.
 *
 * Sizes used are realistic ones: Treadmill GUI 501,532, Analogue face
 * 201,628, Console 137,676, Peak 155,488, a service image about 103,000.
 * Total heap 1,437,088.
 */

#include <gtest/gtest.h>

#include "App/ImageRegion.hpp"

namespace {

// A plausible device layout: region top well above the break.
constexpr uintptr_t kTop   = 0x20200000;
constexpr uintptr_t kBreak = 0x20100000;          // 1 MiB below the top

constexpr size_t kTreadmillGui = 501532;
constexpr size_t kAnalogue     = 201628;
constexpr size_t kConsole      = 137676;
constexpr size_t kPeak         = 155488;

/// Reservations are rounded up to 8, so a test that checks an exact address
/// has to round the same way. The real image sizes below are not multiples of
/// 8, which is why this matters rather than being pedantry.
constexpr size_t r8(size_t n) { return (n + 7u) & ~static_cast<size_t>(7u); }

App::ImageRegion makeRegion()
{
    App::ImageRegion r;
    r.init(kTop);
    return r;
}

} // namespace

TEST(ImageRegion, EmptyRegionLeavesTheKernelEverything)
{
    App::ImageRegion r = makeRegion();

    // The floor is the top when nothing is resident, so _sbrk_r's limit is
    // unchanged from today and the scheme costs nothing until an image loads.
    EXPECT_EQ(r.floor(), kTop);
    EXPECT_EQ(r.count(), 0u);
    EXPECT_EQ(r.bytesInUse(), 0u);
}

TEST(ImageRegion, FirstImageSitsHardAgainstTheTop)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t a = r.allocate(kAnalogue, kBreak);

    ASSERT_NE(a, 0u);
    EXPECT_EQ(a + r8(kAnalogue), kTop) << "an image must pack against the "
                                          "top, not float below it";
    EXPECT_EQ(r.floor(), a);
}

TEST(ImageRegion, ImagesStackDownwardsInOrder)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t a = r.allocate(kAnalogue, kBreak);
    const uintptr_t b = r.allocate(kConsole, kBreak);
    const uintptr_t c = r.allocate(kPeak, kBreak);

    ASSERT_NE(a, 0u);
    ASSERT_NE(b, 0u);
    ASSERT_NE(c, 0u);

    EXPECT_GT(a, b);
    EXPECT_GT(b, c);
    EXPECT_EQ(b + r8(kConsole), a) << "no gap should be left between them";
    EXPECT_EQ(c + r8(kPeak), b);
    EXPECT_EQ(r.floor(), c);
    EXPECT_EQ(r.count(), 3u);
}

// The property the whole design turns on. Freeing a middle image must leave a
// hole that the NEXT image reuses, rather than the next image going below the
// lowest one - which would drag the floor down and hand the kernel less.
TEST(ImageRegion, AMiddleHoleIsReusedRatherThanLoweringTheFloor)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t a = r.allocate(kAnalogue, kBreak);
    const uintptr_t b = r.allocate(kConsole, kBreak);
    const uintptr_t c = r.allocate(kPeak, kBreak);
    ASSERT_NE(c, 0u);

    const uintptr_t floorBefore = r.floor();
    ASSERT_TRUE(r.release(b));

    // Something that fits the hole b left.
    const uintptr_t d = r.allocate(kConsole, kBreak);

    ASSERT_NE(d, 0u);
    EXPECT_EQ(d, b) << "it should land back in the hole";
    EXPECT_EQ(r.floor(), floorBefore) << "the floor must not move";
    (void)a;
    (void)c;
}

// Highest-fit, not best-fit. A small image offered both a small low gap and a
// large high one must take the high one, even though that "wastes" more.
TEST(ImageRegion, PlacementPrefersTheHighestGapNotTheTightestOne)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t big   = r.allocate(200000, kBreak);   // high
    const uintptr_t fence = r.allocate(8,      kBreak);   // keeps the holes apart
    const uintptr_t mid   = r.allocate(8000,   kBreak);
    const uintptr_t low   = r.allocate(200000, kBreak);
    ASSERT_NE(fence, 0u);
    ASSERT_NE(low, 0u);

    // The fence is what makes this a test. Without a live extent between
    // them, releasing big and mid leaves ONE contiguous 208,000 hole, which
    // best fit would also serve from the top - so the test passed under
    // either policy and checked nothing. With the fence there are two
    // distinct holes: 200,000 high, and exactly 8,000 lower down.
    ASSERT_TRUE(r.release(big));
    ASSERT_TRUE(r.release(mid));

    // An 8,000-byte image fits the low gap exactly and the high gap loosely.
    const uintptr_t small = r.allocate(8000, kBreak);

    ASSERT_NE(small, 0u);
    EXPECT_EQ(small + 8000, kTop)
        << "best fit would take the tight low gap; that lowers nothing today "
           "but leaves the high space fragmented for the next large image";
}

TEST(ImageRegion, RefusesWhatWouldCrossTheBreak)
{
    App::ImageRegion r = makeRegion();

    // Region is 1 MiB. Ask for more than that.
    const uintptr_t a = r.allocate((kTop - kBreak) + 8, kBreak);
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(r.floor(), kTop) << "a refusal must not disturb the floor";
    EXPECT_EQ(r.count(), 0u);
}

// The failure from the capture, with the numbers from it: 501,532 wanted,
// and enough total room. Under one arena this failed with 892,816 free.
TEST(ImageRegion, TheCaptureFailureFitsUnderTwoEnds)
{
    App::ImageRegion r = makeRegion();

    // Kernel is using ~541,688 of the 1,437,088 heap, so the break sits that
    // far above the base and the rest is free.
    constexpr uintptr_t heapBase = 0x20000000;
    constexpr uintptr_t top      = heapBase + 1437088;
    constexpr uintptr_t brk      = heapBase + 541688;

    App::ImageRegion big;
    big.init(top);

    const uintptr_t img = big.allocate(kTreadmillGui, brk);

    EXPECT_NE(img, 0u) << "541,688 in use plus a 501,532 image is 1,043,220 "
                          "of 1,437,088 - it fits, and only layout stopped it";
    (void)r;
}

TEST(ImageRegion, ReleaseRejectsAnAddressItNeverGaveOut)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t a = r.allocate(kAnalogue, kBreak);
    ASSERT_NE(a, 0u);

    EXPECT_FALSE(r.release(a + 8)) << "a stray pointer must be reported, not "
                                      "silently ignored";
    EXPECT_TRUE(r.release(a));
    EXPECT_FALSE(r.release(a)) << "a double release must be reported too";
    EXPECT_EQ(r.count(), 0u);
}

TEST(ImageRegion, ReleaseOfNullIsHarmless)
{
    App::ImageRegion r = makeRegion();
    EXPECT_TRUE(r.release(0));
}

TEST(ImageRegion, RefusesPastTheExtentLimit)
{
    App::ImageRegion r = makeRegion();

    // Small images so capacity, not space, is what runs out.
    for (size_t i = 0; i < App::ImageRegion::skMaxImages; i++) {
        ASSERT_NE(r.allocate(64, kBreak), 0u) << "at i=" << i;
    }
    EXPECT_EQ(r.count(), App::ImageRegion::skMaxImages);
    EXPECT_EQ(r.allocate(64, kBreak), 0u) << "the 17th must be refused";
}

// The size that made the alignment rounding wrap. `bytes + 7` overflowed, so
// `want` became 0, every gap test passed trivially, and placement returned
// mTop - one past the region, i.e. the ISR stack - with a zero-length extent.
TEST(ImageRegion, RefusesASizeThatWouldWrapTheAlignmentRounding)
{
    App::ImageRegion r = makeRegion();

    EXPECT_EQ(r.allocate(SIZE_MAX, kBreak), 0u);
    EXPECT_EQ(r.allocate(SIZE_MAX - 6u, kBreak), 0u);
    EXPECT_EQ(r.allocate(SIZE_MAX - 7u, kBreak), 0u) << "this one does not "
            "wrap, but it cannot fit either - it must still be refused";

    EXPECT_EQ(r.count(), 0u) << "a refusal must not record an extent";
    EXPECT_EQ(r.floor(), kTop) << "and must not move the floor";
}

TEST(ImageRegion, AllocationsAreAligned)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t a = r.allocate(1, kBreak);
    const uintptr_t b = r.allocate(7, kBreak);
    const uintptr_t c = r.allocate(4095, kBreak);

    ASSERT_NE(a, 0u);
    ASSERT_NE(b, 0u);
    ASSERT_NE(c, 0u);
    EXPECT_EQ(a % 8u, 0u);
    EXPECT_EQ(b % 8u, 0u);
    EXPECT_EQ(c % 8u, 0u);
}

TEST(ImageRegion, LargestFreeBlockTracksTheGaps)
{
    App::ImageRegion r = makeRegion();

    EXPECT_EQ(r.largestFreeBlock(kBreak), kTop - kBreak);

    const uintptr_t a = r.allocate(200000, kBreak);
    ASSERT_NE(a, 0u);
    EXPECT_EQ(r.largestFreeBlock(kBreak), (kTop - kBreak) - 200000);

    // With 800,000 of the 1,048,576 region taken, the gap down to the break
    // is 248,576 - still larger than the 200,000 hole freeing `a` leaves. The
    // largest block is the low gap, and saying so is the point: a freed hole
    // is not automatically the biggest thing available.
    const uintptr_t b = r.allocate(600000, kBreak);
    ASSERT_NE(b, 0u);
    ASSERT_TRUE(r.release(a));
    EXPECT_EQ(r.largestFreeBlock(kBreak), 248576u);

    // Take the low gap away and the freed hole becomes the largest.
    const uintptr_t c = r.allocate(248576, kBreak);
    ASSERT_NE(c, 0u);
    EXPECT_EQ(r.largestFreeBlock(kBreak), 200000u);
}

// A moving break must be honoured on every call, not remembered: the kernel
// grows between image loads and a stale copy would let an image be placed
// into memory the kernel has since taken.
TEST(ImageRegion, ARisingBreakShrinksWhatCanBePlaced)
{
    App::ImageRegion r = makeRegion();

    const uintptr_t lowBreak  = kTop - 600000;
    const uintptr_t highBreak = kTop - 300000;

    EXPECT_NE(r.allocate(250000, lowBreak), 0u);

    // Kernel has since grown; the same size no longer fits below what is left.
    EXPECT_EQ(r.allocate(250000, highBreak), 0u);
}
