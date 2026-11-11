/**
 * Host unit tests for Tools::CopyWordwise and Tools::ZeroWordwise — the
 * word-at-a-time byte movers the kernel uses instead of the shared libc, whose
 * memcpy and memset are plain byte loops it cannot replace.
 *
 * The fast path is only taken when the pointers are word-aligned, so the tests
 * exercise both it and the libc fallback, and check that neither writes past
 * the length it was given.
 */

#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "WordMove.hpp"

namespace {

constexpr uint8_t kGuard = 0xA5;

// A buffer with guard bytes either side of the region under test, so an
// overrun by even one byte is visible rather than merely likely to crash.
class GuardedBuffer {

public:

    explicit GuardedBuffer(size_t bytes, size_t offset = 0)
        : mBytes(bytes)
        , mOffset(offset)
        , mStore(kGuardBytes + offset + bytes + kGuardBytes, kGuard)
    {
    }

    uint8_t* data()
    {
        return mStore.data() + kGuardBytes + mOffset;
    }

    bool guardsIntact() const
    {
        for (size_t i = 0; i < kGuardBytes; ++i) {
            if (mStore[i] != kGuard) {
                return false;
            }
            if (mStore[mStore.size() - 1 - i] != kGuard) {
                return false;
            }
        }

        return true;
    }

    std::vector<uint8_t> contents() const
    {
        const uint8_t *begin = mStore.data() + kGuardBytes + mOffset;
        return std::vector<uint8_t>(begin, begin + mBytes);
    }

private:

    static constexpr size_t kGuardBytes = 8;

    size_t               mBytes;
    size_t               mOffset;
    std::vector<uint8_t> mStore;

};

std::vector<uint8_t> pattern(size_t bytes)
{
    std::vector<uint8_t> v(bytes);
    std::iota(v.begin(), v.end(), static_cast<uint8_t>(1));
    return v;
}

} // namespace

// A frame is a whole number of words, which is the only case AppSurface hits.
TEST(WordMove, CopiesAWholeNumberOfWords)
{
    const std::vector<uint8_t> src = pattern(64);
    GuardedBuffer dst(src.size());

    Tools::CopyWordwise(dst.data(), src.data(), src.size());

    EXPECT_EQ(src, dst.contents());
    EXPECT_TRUE(dst.guardsIntact());
}

// The case the extraction exposed: as a private helper this length could not
// occur, so a trailing part-word was silently dropped. Shared, it has to work.
TEST(WordMove, CopiesATrailingPartWord)
{
    for (size_t bytes = 1; bytes <= 19; ++bytes) {
        const std::vector<uint8_t> src = pattern(bytes);
        GuardedBuffer dst(bytes);

        Tools::CopyWordwise(dst.data(), src.data(), bytes);

        EXPECT_EQ(src, dst.contents()) << "length " << bytes;
        EXPECT_TRUE(dst.guardsIntact()) << "length " << bytes;
    }
}

TEST(WordMove, CopiesWithEitherEndUnaligned)
{
    // offset 1..3 puts the buffer off a word boundary, which sends the call
    // down the libc fallback. The result has to be identical either way.
    for (size_t offset = 1; offset <= 3; ++offset) {
        const std::vector<uint8_t> src = pattern(37);

        GuardedBuffer unalignedDst(src.size(), offset);
        Tools::CopyWordwise(unalignedDst.data(), src.data(), src.size());
        EXPECT_EQ(src, unalignedDst.contents()) << "dst offset " << offset;
        EXPECT_TRUE(unalignedDst.guardsIntact()) << "dst offset " << offset;

        GuardedBuffer unalignedSrc(src.size() + offset);
        memcpy(unalignedSrc.data() + offset, src.data(), src.size());

        GuardedBuffer alignedDst(src.size());
        Tools::CopyWordwise(alignedDst.data(), unalignedSrc.data() + offset, src.size());
        EXPECT_EQ(src, alignedDst.contents()) << "src offset " << offset;
        EXPECT_TRUE(alignedDst.guardsIntact()) << "src offset " << offset;
    }
}

TEST(WordMove, CopyOfNothingTouchesNothing)
{
    const std::vector<uint8_t> src = pattern(4);
    GuardedBuffer dst(4);

    const std::vector<uint8_t> before = dst.contents();
    Tools::CopyWordwise(dst.data(), src.data(), 0);

    EXPECT_EQ(before, dst.contents());
    EXPECT_TRUE(dst.guardsIntact());
}

TEST(WordMove, ZeroesAWholeNumberOfWords)
{
    GuardedBuffer dst(64);
    memset(dst.data(), 0x5C, 64);

    Tools::ZeroWordwise(dst.data(), 64);

    EXPECT_EQ(std::vector<uint8_t>(64, 0), dst.contents());
    EXPECT_TRUE(dst.guardsIntact());
}

TEST(WordMove, ZeroesATrailingPartWord)
{
    for (size_t bytes = 1; bytes <= 19; ++bytes) {
        GuardedBuffer dst(bytes);
        memset(dst.data(), 0x5C, bytes);

        Tools::ZeroWordwise(dst.data(), bytes);

        EXPECT_EQ(std::vector<uint8_t>(bytes, 0), dst.contents()) << "length " << bytes;
        EXPECT_TRUE(dst.guardsIntact()) << "length " << bytes;
    }
}

TEST(WordMove, ZeroesWithAnUnalignedDestination)
{
    for (size_t offset = 1; offset <= 3; ++offset) {
        GuardedBuffer dst(37, offset);
        memset(dst.data(), 0x5C, 37);

        Tools::ZeroWordwise(dst.data(), 37);

        EXPECT_EQ(std::vector<uint8_t>(37, 0), dst.contents()) << "offset " << offset;
        EXPECT_TRUE(dst.guardsIntact()) << "offset " << offset;
    }
}

TEST(WordMove, ZeroOfNothingTouchesNothing)
{
    GuardedBuffer dst(4);
    memset(dst.data(), 0x5C, 4);

    Tools::ZeroWordwise(dst.data(), 0);

    EXPECT_EQ(std::vector<uint8_t>(4, 0x5C), dst.contents());
    EXPECT_TRUE(dst.guardsIntact());
}
