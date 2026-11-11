/**
 * Host unit tests for AppSurfaceInfo — how a GUI reader is told where a running
 * app's frame is, and how it samples the owner and the frame counter together.
 *
 * sample() exists because those are two atomics and this core has no 64-bit
 * exclusives, so they cannot be read in one go. A handover landing between the
 * two reads would pair one app's identity with another's counter, and the
 * screen would act on a state that never existed. Reading the owner on both
 * sides and reporting failure if it moved is what prevents that.
 *
 * The failure is invisible on a bench test — it needs a handover to land
 * between two adjacent loads — which is why it is pinned here instead.
 */

#include <atomic>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

#include "AppSurfaceInfo.hpp"

namespace {

// Stands in for AppSurface: the two atomics a reader watches, plus somewhere
// for pixels to point. Publishes in the order AppSurface does -- the counter
// first, then the owner with release -- so whoever sees a new owner sees the
// reset counter with it.
class FakeSurface {

public:

    AppSurfaceInfo info() const
    {
        return AppSurfaceInfo { mPixels, 240, 240, &mGeneration, &mOwner };
    }

    void take(uint32_t pid)
    {
        mGeneration.store(0, std::memory_order_relaxed);
        mOwner.store(pid, std::memory_order_release);
    }

    void release()
    {
        mOwner.store(0, std::memory_order_release);
    }

    void drew(uint32_t frame)
    {
        mGeneration.store(frame, std::memory_order_relaxed);
    }

private:

    uint8_t               mPixels[4] { };
    std::atomic<uint32_t> mGeneration { 0 };
    std::atomic<uint32_t> mOwner { 0 };

};

// Frame counters are per-owner and restart at zero on a handover, so a bare
// number says nothing about which app produced it. Encoding the owner in the
// counter is what lets a test recognise a mismatched pair when it sees one.
constexpr uint32_t kFramesPerOwner = 1000;

uint32_t frameOf(uint32_t pid, uint32_t n)
{
    return pid * kFramesPerOwner + n;
}

uint32_t ownerOfFrame(uint32_t frame)
{
    return frame / kFramesPerOwner;
}

// How much of each side to run. The reader's count is what bounds the test:
// the writer keeps handing the surface over until the reader has finished, so
// the two overlap for every sample rather than for however long the writer
// happens to take. At namespace scope because MSVC will not let a lambda use a
// constexpr local without capturing it.
constexpr uint32_t kSamples    = 200000;
constexpr uint32_t kFrames     = 20;
constexpr uint32_t kMaxOwnerId = 999;

} // namespace

TEST(AppSurfaceInfo, DefaultDescribesNoSurface)
{
    const AppSurfaceInfo info {};

    uint32_t owner = 7;
    uint32_t frame = 7;

    EXPECT_FALSE(info.sample(owner, frame));
    EXPECT_EQ(0u, owner);
    EXPECT_EQ(0u, frame);
    EXPECT_EQ(nullptr, info.pixels);
}

TEST(AppSurfaceInfo, SampleRefusesAPartialDescription)
{
    std::atomic<uint32_t> counter { 3 };
    uint8_t               pixels[4] { };

    // Either half missing means there is nothing coherent to answer with.
    const AppSurfaceInfo noOwner { pixels, 240, 240, &counter, nullptr };
    const AppSurfaceInfo noCounter { pixels, 240, 240, nullptr, &counter };

    uint32_t owner = 0;
    uint32_t frame = 0;

    EXPECT_FALSE(noOwner.sample(owner, frame));
    EXPECT_FALSE(noCounter.sample(owner, frame));
}

TEST(AppSurfaceInfo, SamplePairsOwnerWithItsOwnCounter)
{
    FakeSurface surface;
    const AppSurfaceInfo info = surface.info();

    uint32_t owner = 0;
    uint32_t frame = 0;

    // Nobody holds it yet.
    ASSERT_TRUE(info.sample(owner, frame));
    EXPECT_EQ(0u, owner);
    EXPECT_EQ(0u, frame);

    // Taken, nothing drawn: zero is what tells a reader the new owner has not
    // produced anything, rather than that there is no owner.
    surface.take(0x11u);
    ASSERT_TRUE(info.sample(owner, frame));
    EXPECT_EQ(0x11u, owner);
    EXPECT_EQ(0u, frame);

    surface.drew(frameOf(0x11u, 1));
    ASSERT_TRUE(info.sample(owner, frame));
    EXPECT_EQ(0x11u, owner);
    EXPECT_EQ(frameOf(0x11u, 1), frame);
}

TEST(AppSurfaceInfo, HandoverResetsTheCounterWithTheOwner)
{
    FakeSurface surface;
    const AppSurfaceInfo info = surface.info();

    surface.take(0x11u);
    surface.drew(frameOf(0x11u, 5));

    surface.take(0x22u);

    uint32_t owner = 0;
    uint32_t frame = 0;

    ASSERT_TRUE(info.sample(owner, frame));
    EXPECT_EQ(0x22u, owner);
    EXPECT_EQ(0u, frame) << "the incoming app must not inherit a count it did not produce";
}

TEST(AppSurfaceInfo, ReleaseLeavesTheCounterAlone)
{
    FakeSurface surface;
    const AppSurfaceInfo info = surface.info();

    surface.take(0x11u);
    surface.drew(frameOf(0x11u, 5));
    surface.release();

    uint32_t owner = 0;
    uint32_t frame = 0;

    // Giving the surface up does not disturb the pixels or the count -- a
    // screen still showing that app holds its last frame until it leaves.
    ASSERT_TRUE(info.sample(owner, frame));
    EXPECT_EQ(0u, owner);
    EXPECT_EQ(frameOf(0x11u, 5), frame);
}

// The one the double read is for. A writer hands the surface between apps while
// a reader samples, and every pair the reader accepts has to be one that
// actually existed: the counter must belong to the owner returned with it.
//
// This cannot force the race, only make it likely, so it proves nothing on a
// run where the interleaving never happens -- but it cannot fail spuriously
// either, because it asserts an invariant rather than an occurrence.
TEST(AppSurfaceInfo, SampleNeverPairsAnOwnerWithAnotherAppsCounter)
{
    FakeSurface surface;
    const AppSurfaceInfo info = surface.info();

    // The reader drives. Letting the writer decide when to stop would leave the
    // reader free to sample zero times -- the writer can finish before the
    // reader is scheduled at all -- which is a test that passes by not running.
    std::atomic<bool> readerDone { false };

    std::thread writer([&surface, &readerDone]() {
        uint32_t pid = 1;

        while (!readerDone.load(std::memory_order_acquire)) {
            surface.take(pid);

            for (uint32_t n = 1; n <= kFrames; ++n) {
                surface.drew(frameOf(pid, n));
            }

            pid = (pid % kMaxOwnerId) + 1;
        }
    });

    uint32_t accepted    = 0;
    uint32_t rejected    = 0;
    uint32_t badOwner    = 0;
    uint32_t badFrame    = 0;
    bool     sawBadPair  = false;

    for (uint32_t i = 0; i < kSamples; ++i) {
        uint32_t owner = 0;
        uint32_t frame = 0;

        if (!info.sample(owner, frame)) {
            ++rejected;
            continue;
        }

        ++accepted;

        // A zero counter is the one pair that is always safe: it says the owner
        // has drawn nothing, whichever owner it turns out to be.
        if (frame == 0 || owner == ownerOfFrame(frame)) {
            continue;
        }

        // Recorded rather than asserted here: an assertion returns from the
        // test, and leaving the writer unjoined would terminate the process
        // instead of reporting the failure.
        sawBadPair = true;
        badOwner   = owner;
        badFrame   = frame;
        break;
    }

    readerDone.store(true, std::memory_order_release);
    writer.join();

    EXPECT_FALSE(sawBadPair)
        << "accepted a counter from app " << ownerOfFrame(badFrame)
        << " (frame " << badFrame << ") alongside owner " << badOwner;

    // Structural, so it holds however the two threads interleave: the reader
    // ran, and every sample was either taken or refused.
    if (!sawBadPair) {
        EXPECT_EQ(kSamples, accepted + rejected);
    }

    // Not asserted: on a run where no handover landed inside a read, nothing is
    // rejected and the test simply proves less.
    (void) rejected;
}
