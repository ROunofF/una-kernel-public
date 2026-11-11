/**
 ******************************************************************************
 * @file    Bmi270FifoGrid_test.cpp
 * @brief   Host tests for where a stream's FIFO frames stand on the BMI270's
 *          counter
 ******************************************************************************
 */

/**
 * Host unit tests for where a stream's FIFO frames stand on the BMI270's
 * counter. The sequences marked "as read from the part" are what the part
 * gave on a watch: the frame counts, the counter at the end of each read and
 * the counter values its configuration frames carried.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "Hardware/BMI270/Bmi270Odr.hpp"
#include "Hardware/BMI270/FifoGrid.hpp"

namespace {

using Hardware::Bmi270Fifo::StreamGrid;
using Hardware::Bmi270Fifo::isAfter;
using Hardware::Bmi270Fifo::kCounterMask;

// Register values of the rates used here.
constexpr uint8_t kOdr0p78 = 0x01;
constexpr uint8_t kOdr25   = 0x06;
constexpr uint8_t kOdr50   = 0x07;
constexpr uint8_t kOdr100  = 0x08;
constexpr uint8_t kOdr200  = 0x09;
constexpr uint8_t kOdr400  = 0x0A;
constexpr uint8_t kOdr1600 = 0x0C;

uint32_t lastOf(const StreamGrid::Batch& batch)
{
    return (batch.first + (batch.count - 1u) * batch.step) & kCounterMask;
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Odr::periodCounts
// ---------------------------------------------------------------------------

// One count is 39.0625 us, so a period in counts is its microseconds times
// 16 / 625.
TEST(Bmi270FifoGrid, PeriodInCountsMatchesThePeriodInMicroseconds)
{
    using namespace Hardware::Bmi270Odr;

    for (uint8_t odr = kSlowest; odr <= kFastest; ++odr) {
        EXPECT_EQ(periodCounts(odr) * 625u, periodUs(odr) * 16u) << static_cast<int>(odr);
    }

    EXPECT_EQ(periodCounts(kFastest), 16u);
    EXPECT_EQ(periodCounts(kSlowest), 32768u);
    EXPECT_EQ(periodCounts(0), 0u);
    EXPECT_EQ(periodCounts(kFastest + 1), 0u);
}

TEST(Bmi270FifoGrid, LaterMeansAheadByLessThanHalfTheCounter)
{
    EXPECT_TRUE(isAfter(10, 9));
    EXPECT_FALSE(isAfter(9, 10));
    EXPECT_FALSE(isAfter(10, 10));
    EXPECT_TRUE(isAfter(5, kCounterMask - 5));          // over the wrap
    EXPECT_FALSE(isAfter(kCounterMask - 5, 5));
}

// ---------------------------------------------------------------------------
// A steady stream
// ---------------------------------------------------------------------------

// As read from the part: the accelerometer at 1600 Hz, the FIFO switched on
// with the first frame said to stand at 3776.
TEST(Bmi270FifoGrid, NewestFrameIsTheLastGridPointBeforeTheRead)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);
    grid.notBefore(3776);

    const StreamGrid::Batch first = grid.layBack(183, 6695);

    EXPECT_EQ(first.step, 16u);
    EXPECT_EQ(first.count, 183u);
    EXPECT_EQ(first.dropped, 0u);
    EXPECT_EQ(first.first, 3776u);
    EXPECT_EQ(lastOf(first), 6688u);

    // The read after it goes on where this one ended.
    EXPECT_TRUE(grid.continues(183, 9627));

    const StreamGrid::Batch second = grid.layBack(183, 9627);

    EXPECT_EQ(second.first, 6704u);
    EXPECT_EQ(lastOf(second), 9616u);
}

TEST(Bmi270FifoGrid, AReadThatFallsOnAGridPointHasThatFrame)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    const StreamGrid::Batch batch = grid.layBack(1, 256 * 40);

    EXPECT_EQ(batch.first, 256u * 40u);
}

TEST(Bmi270FifoGrid, TheCounterWrapsInsideABatch)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    // The read came 300 counts after the wrap: frames at 0 and 256 after it,
    // two before.
    const StreamGrid::Batch batch = grid.layBack(4, 300);

    EXPECT_EQ(batch.first, (kCounterMask + 1u) - 512u);
    EXPECT_EQ(lastOf(batch), 256u);

    EXPECT_TRUE(grid.continues(2, 800));
    EXPECT_EQ(grid.layBack(2, 800).first, 512u);
}

// ---------------------------------------------------------------------------
// A change of rate
// ---------------------------------------------------------------------------

// As read from the part: 1600 Hz to 100 Hz. Four frames at the old rate
// follow the write, then the part says the new rate starts at 121856.
TEST(Bmi270FifoGrid, FramesBeforeAChangeGoOnAtTheOldRate)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    EXPECT_EQ(lastOf(grid.layBack(13, 121538)), 121536u);

    const StreamGrid::Batch old = grid.layForward(4);

    EXPECT_EQ(old.step, 16u);
    EXPECT_EQ(old.first, 121552u);
    EXPECT_EQ(lastOf(old), 121600u);

    grid.setOdr(kOdr100);
    grid.notBefore(121856);

    const StreamGrid::Batch now = grid.layBack(6, 123174);

    EXPECT_EQ(now.step, 256u);
    EXPECT_EQ(now.first, 121856u);
    EXPECT_EQ(lastOf(now), 123136u);
    EXPECT_EQ(now.dropped, 0u);
}

// As read from the part: 100 Hz to 25 Hz. The part gave no frame at the old
// rate after the write, though one was due before the new rate started.
TEST(Bmi270FifoGrid, ASlowerRateStartsWhereThePartSays)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(3, 131639)), 131584u);

    grid.setOdr(kOdr25);
    grid.notBefore(132096);

    const StreamGrid::Batch now = grid.layBack(2, 133152);

    EXPECT_EQ(now.first, 132096u);
    EXPECT_EQ(lastOf(now), 133120u);
}

// As read from the part: 25 Hz to 200 Hz. The faster rate starts at once, off
// the old rate's grid.
TEST(Bmi270FifoGrid, AFasterRateStartsOffTheOldGrid)
{
    StreamGrid grid;
    grid.setOdr(kOdr25);

    EXPECT_EQ(lastOf(grid.layBack(1, 144437)), 144384u);

    grid.setOdr(kOdr200);
    grid.notBefore(144640);

    const StreamGrid::Batch now = grid.layBack(20, 147133);

    EXPECT_EQ(now.first, 144640u);
    EXPECT_EQ(lastOf(now), 147072u);
}

// As read from the part: both streams from 50 Hz to 400 Hz in one go. The
// part names one counter value for the change, and the accelerometer's first
// frame at the new rate stands two frames after it.
TEST(Bmi270FifoGrid, EachStreamIsPlacedFromTheReadNotFromTheChange)
{
    StreamGrid accel;
    StreamGrid gyro;

    accel.setOdr(kOdr50);
    gyro.setOdr(kOdr50);
    EXPECT_EQ(lastOf(accel.layBack(5, 201246)), 201216u);
    EXPECT_EQ(lastOf(gyro.layBack(5, 201246)), 201216u);

    EXPECT_EQ(lastOf(accel.layForward(1)), 201728u);
    EXPECT_EQ(lastOf(gyro.layForward(1)), 201728u);

    accel.setOdr(kOdr400);
    gyro.setOdr(kOdr400);
    accel.notBefore(201920);
    gyro.notBefore(201920);

    EXPECT_EQ(gyro.layBack(42, 204601).first, 201920u);
    EXPECT_EQ(accel.layBack(40, 204601).first, 202048u);
}

// As read from the part: the gyroscope from 1600 Hz to 25 Hz. Fifteen frames
// at the old rate follow the read that ended at 201495, and then the stream
// is silent: the read they come in ends 700 counts after the last of them.
// Counted back from that read they would stand where no frame was taken.
TEST(Bmi270FifoGrid, AStreamThatStoppedIsPlacedFromItsLastFrame)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    EXPECT_EQ(lastOf(grid.layBack(13, 201495)), 201488u);

    // Held to the old rate, and nothing in the frames says otherwise.
    EXPECT_FALSE(grid.outgrew(kOdr25, 15, 202419));

    const StreamGrid::Batch old = grid.layForward(15);

    EXPECT_EQ(old.first, 201504u);
    EXPECT_EQ(lastOf(old), 201728u);

    grid.setOdr(kOdr25);
    grid.notBefore(202752);

    EXPECT_EQ(grid.layBack(3, 204860).first, 202752u);
}

// As read from the part: the accelerometer from 1600 Hz to 12.5 Hz. Its last
// frame at the old rate stands at 290816; the read that brings it ends 19
// counts later, past the next point of the old rate's grid.
TEST(Bmi270FifoGrid, TheLastFrameAtTheOldRateIsNotMovedToTheRead)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    EXPECT_EQ(lastOf(grid.layBack(2, 290805)), 290800u);
    EXPECT_FALSE(grid.outgrew(0x05, 1, 290835));
    EXPECT_EQ(grid.layForward(1).first, 290816u);
}

// As read from the part: the accelerometer from 400 Hz to 1600 Hz as the
// gyroscope joins it. One read holds two frames at the old rate, the part's
// word that the new rate starts at 329488, 55 frames, and its word that the
// next frame - the first with both sensors - stands at 330368. The 55 end
// just short of that.
TEST(Bmi270FifoGrid, AStretchEndsJustShortOfWhereTheNextStarts)
{
    StreamGrid grid;
    grid.setOdr(kOdr400);

    EXPECT_EQ(lastOf(grid.layBack(37, 329381)), 329344u);
    EXPECT_EQ(lastOf(grid.layForward(2)), 329472u);

    grid.setOdr(kOdr1600);
    grid.notBefore(329488);

    const StreamGrid::Batch alone = grid.layBack(55, 330368 - 1);

    EXPECT_EQ(alone.first, 329488u);
    EXPECT_EQ(lastOf(alone), 330352u);

    grid.notBefore(330368);

    const StreamGrid::Batch paired = grid.layBack(86, 331737);

    EXPECT_EQ(paired.first, 330368u);
    EXPECT_EQ(paired.dropped, 0u);
}

// ---------------------------------------------------------------------------
// A read that did not reach the end of the FIFO
// ---------------------------------------------------------------------------

TEST(Bmi270FifoGrid, WithoutTheCounterABatchGoesOnFromTheLastFrame)
{
    StreamGrid grid;
    grid.setOdr(kOdr200);

    EXPECT_EQ(lastOf(grid.layBack(10, 12800 + 5)), 12800u);

    const StreamGrid::Batch batch = grid.layForward(3);

    EXPECT_EQ(batch.first, 12928u);
    EXPECT_EQ(lastOf(batch), 13184u);

    // And the next read to the end takes it up from there.
    EXPECT_TRUE(grid.continues(2, 13440 + 9));
    EXPECT_EQ(grid.layBack(2, 13440 + 9).first, 13312u);
}

TEST(Bmi270FifoGrid, WithoutTheCounterAStreamStartsWhereThePartSaid)
{
    StreamGrid grid;
    grid.setOdr(kOdr200);
    grid.notBefore(1000);

    // 1000 is not a multiple of the period: the first one after it.
    EXPECT_EQ(grid.layForward(2).first, 1024u);
}

TEST(Bmi270FifoGrid, AfterAChangeForwardFramesStandNoEarlierThanThePartSaid)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    EXPECT_EQ(lastOf(grid.layBack(1, 1600)), 1600u);

    grid.setOdr(kOdr100);
    grid.notBefore(2048);

    EXPECT_EQ(grid.layForward(1).first, 2048u);
}

TEST(Bmi270FifoGrid, WithNothingToGoByABatchIsLeftOut)
{
    StreamGrid grid;
    grid.setOdr(kOdr200);

    const StreamGrid::Batch batch = grid.layForward(5);

    EXPECT_EQ(batch.count, 0u);
    EXPECT_EQ(batch.dropped, 5u);
}

TEST(Bmi270FifoGrid, WithNoRateABatchIsLeftOut)
{
    StreamGrid grid;

    EXPECT_EQ(grid.layBack(5, 1000).count, 0u);
    EXPECT_EQ(grid.layForward(5).count, 0u);
    EXPECT_FALSE(grid.continues(5, 1000));
}

// ---------------------------------------------------------------------------
// Values only increase
// ---------------------------------------------------------------------------

// Frames lost by the part show as a wider step from one batch to the next.
TEST(Bmi270FifoGrid, LostFramesLeaveAGapBetweenBatches)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(4, 2560)), 2560u);
    EXPECT_FALSE(grid.continues(4, 5120));

    const StreamGrid::Batch batch = grid.layBack(4, 5120);

    EXPECT_EQ(batch.first, 4352u);
    EXPECT_EQ(batch.dropped, 0u);
}

// A batch placed forward may have been put later than its frames really
// stood. The read that next reaches the end shows it: its oldest frames
// would stand at or before one given out, and are left out.
TEST(Bmi270FifoGrid, FramesThatWouldNotBeLaterAreLeftOut)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(1, 2560)), 2560u);
    EXPECT_EQ(lastOf(grid.layForward(3)), 3328u);

    // Five frames ending at 3840: 2816, 3072, 3328 are not later than 3328.
    const StreamGrid::Batch batch = grid.layBack(5, 3840);

    EXPECT_EQ(batch.dropped, 3u);
    EXPECT_EQ(batch.count, 2u);
    EXPECT_EQ(batch.first, 3584u);
    EXPECT_EQ(lastOf(batch), 3840u);
}

TEST(Bmi270FifoGrid, ABatchWhollyNotLaterIsLeftOut)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(1, 2560)), 2560u);

    const StreamGrid::Batch batch = grid.layBack(2, 2560 + 10);

    EXPECT_EQ(batch.count, 0u);
    EXPECT_EQ(batch.dropped, 2u);

    // What was given out stands, and is not given out again after a batch
    // that ended before it.
    EXPECT_EQ(grid.layBack(2, 2100).count, 0u);
    EXPECT_EQ(grid.layBack(1, 2560 + 10).count, 0u);
    EXPECT_TRUE(grid.continues(1, 2816));
}

TEST(Bmi270FifoGrid, ARestartForgetsWhereTheStreamStood)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(1, 100000)), 99840u);

    grid.restart();

    const StreamGrid::Batch batch = grid.layBack(2, 600);

    EXPECT_EQ(batch.count, 2u);
    EXPECT_EQ(batch.first, 256u);
}

// ---------------------------------------------------------------------------
// A change the part's word for was lost
// ---------------------------------------------------------------------------

// A stream that went from 0.78 Hz to 1600 Hz gives hundreds of frames where
// the old rate has one due, and they continue it at the new rate.
TEST(Bmi270FifoGrid, FramesThatContinueAtTheNewRateWereTakenAtIt)
{
    StreamGrid grid;
    grid.setOdr(kOdr0p78);

    EXPECT_EQ(lastOf(grid.layBack(1, 98322)), 98304u);
    EXPECT_FALSE(grid.continues(182, 101230));

    // 98304 + 182 * 16 = 101216, the last grid point before the read.
    EXPECT_TRUE(grid.outgrew(kOdr1600, 182, 101230));
}

// More frames than the old rate has room for up to the read.
TEST(Bmi270FifoGrid, MoreFramesThanTheOldRateHasRoomFor)
{
    StreamGrid grid;
    grid.setOdr(kOdr25);

    EXPECT_EQ(lastOf(grid.layBack(1, 10240 + 3)), 10240u);

    // A frame was lost on the way, so they do not continue at the new rate
    // either; but 20 frames at 25 Hz would end 20480 counts on, and the read
    // came after 2728.
    EXPECT_TRUE(grid.outgrew(kOdr200, 20, 10240 + 2728));
}

// A slower rate, the part's word for it lost: its frames come long after the
// old rate's stopped.
TEST(Bmi270FifoGrid, FramesLongAfterTheOldRateStopped)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    EXPECT_EQ(lastOf(grid.layBack(4, 32752 + 5)), 32752u);

    // The first frame at 0.78 Hz, one of its periods on: the part may still
    // be about to say so.
    EXPECT_FALSE(grid.outgrew(kOdr0p78, 1, 65536 + 20000));

    // Three of its periods on, it would have.
    EXPECT_TRUE(grid.outgrew(kOdr0p78, 1, 32768 + 3 * 32768 + 20));
}

TEST(Bmi270FifoGrid, AStreamAtTheRateWrittenHasNothingToOutgrow)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_EQ(lastOf(grid.layBack(1, 2560)), 2560u);
    EXPECT_FALSE(grid.outgrew(kOdr100, 50, 2560 + 256));
    EXPECT_FALSE(grid.outgrew(kOdr25, 0, 2560 + 256));
}

// Changed before its first frame was given out: the old rate has no hold.
TEST(Bmi270FifoGrid, AStreamWithNoFrameGivenTakesTheRateWritten)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    EXPECT_TRUE(grid.outgrew(kOdr25, 2, 5000));
}

} // namespace

// The counter wraps every 655 s. A stream that comes back more than half of
// that after it stopped stands, by the counter, behind where it last was. Held
// to that old place it would have every frame left out until the counter came
// round; started anew it is given out at once.
TEST(Bmi270FifoGrid, AStreamStartedAnewIsGivenOutWhereverTheCounterIs)
{
    constexpr uint32_t kPeriod = 256;       // 100 Hz
    constexpr uint32_t kPause  = 9000000;   // 351 s of counts: over half of the counter's range

    StreamGrid held;
    held.setOdr(kOdr100);
    ASSERT_EQ(held.layBack(4, 100 * kPeriod).count, 4);

    StreamGrid anew = held;
    anew.restart();

    const uint32_t read = ((100 * kPeriod) + kPause) & kCounterMask;

    EXPECT_EQ(held.layBack(10, read).count, 0);

    const StreamGrid::Batch batch = anew.layBack(10, read);

    EXPECT_EQ(batch.count, 10);
    EXPECT_EQ(batch.dropped, 0);
    EXPECT_EQ((batch.first + 9u * batch.step) & kCounterMask, read & ~(kPeriod - 1u));
}

// The part's word of a slower rate lost, and one frame read. It stands one
// period of the new rate after the last frame, and too soon after the old
// rate's frames for the wait alone to tell: that it continues the stream at
// the new rate is what says it was taken at it.
TEST(Bmi270FifoGrid, OneFrameThatContinuesAtTheSlowerRateWasTakenAtIt)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);
    ASSERT_EQ(grid.layBack(4, 100u * 256u).count, 4);

    EXPECT_TRUE(grid.outgrew(kOdr100, 1, (101u * 256u) + 5u));
    EXPECT_FALSE(grid.outgrew(kOdr100, 1, (100u * 256u) + 200u));
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Fifo::placeFrames
// ---------------------------------------------------------------------------

using Hardware::Bmi270Fifo::placeFrames;

// A running stream at the rate written is counted back from where the
// stretch ends.
TEST(Bmi270FifoGrid, ARunningStreamIsPlacedBackFromTheEndOfTheStretch)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    const uint32_t          end   = (200u * 256u) + 7u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 5, &end);

    EXPECT_EQ(batch.count, 5);
    EXPECT_EQ(lastOf(batch), 200u * 256u);
    EXPECT_EQ(batch.step, 256u);
}

// With no end known, or the stream switched off, the frames go on from the
// last one given out: the read's end says nothing of where they stand.
TEST(Bmi270FifoGrid, WithNoEndOrSwitchedOffAStreamGoesOnFromItsLastFrame)
{
    for (const bool live : { true, false }) {
        StreamGrid grid;
        grid.setOdr(kOdr100);

        const uint32_t first = 200u * 256u;
        ASSERT_EQ(placeFrames(grid, kOdr100, true, 5, &first).count, 5);

        // Long after: counted back from here the frames would stand 100
        // periods on.
        const uint32_t          later = 300u * 256u;
        const StreamGrid::Batch batch = placeFrames(grid, kOdr100, live, 3, live ? nullptr : &later);

        EXPECT_EQ(batch.count, 3) << "live " << live;
        EXPECT_EQ(batch.first, 201u * 256u) << "live " << live;
    }
}

// A slower rate written and not yet in effect: the frames that still come
// are at the old rate and go on from the last one, at the old period.
TEST(Bmi270FifoGrid, AStreamWaitingForItsNewRateIsPlacedAtTheOldOne)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    const uint32_t first = 100u * 256u;
    ASSERT_EQ(placeFrames(grid, kOdr1600, true, 4, &first).count, 4);

    // Three more frames 16 counts apart, a read just after them.
    const uint32_t          end   = first + (3u * 16u) + 3u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 3, &end);

    EXPECT_EQ(batch.step, 16u);
    EXPECT_EQ(batch.first, first + 16u);
    EXPECT_EQ(grid.odr(), kOdr1600);
}

// The same, the part's word lost: once the frames can only be at the new
// rate the stream takes it and is counted back from the end.
TEST(Bmi270FifoGrid, AStreamThatOutgrewItsOldRateTakesTheOneWritten)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    const uint32_t first = 100u * 256u;
    ASSERT_EQ(placeFrames(grid, kOdr1600, true, 4, &first).count, 4);

    const uint32_t          end   = (110u * 256u) + 9u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 10, &end);

    EXPECT_EQ(grid.odr(), kOdr100);
    EXPECT_EQ(batch.step, 256u);
    EXPECT_EQ(batch.count, 10);
    EXPECT_EQ(lastOf(batch), 110u * 256u);
}

// ---------------------------------------------------------------------------
// The rate of a stretch, read off its frames
// ---------------------------------------------------------------------------

// A stream that ran outside the FIFO comes into it just as a slower rate is
// written. The part goes on at the old rate until the new one takes effect,
// and the frames show it: three of them in 45 counts are 16 counts apart,
// whatever was written.
TEST(Bmi270FifoGrid, AStreamJoiningAtASlowerRateIsPlacedAtTheRateItStillRunsAt)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);
    grid.setOdr(kOdr25);
    grid.restart();

    grid.notBefore(16528);

    uint32_t          end   = 16573;
    StreamGrid::Batch batch = placeFrames(grid, kOdr25, true, 3, &end);

    EXPECT_EQ(batch.count, 3);
    EXPECT_EQ(batch.first, 16528u);
    EXPECT_EQ(batch.step, 16u);
    EXPECT_EQ(grid.odr(), kOdr1600);

    // The next read, still before the new rate: on from there.
    end   = 16606;
    batch = placeFrames(grid, kOdr25, true, 2, &end);

    EXPECT_EQ(batch.count, 2);
    EXPECT_EQ(batch.first, 16576u);
    EXPECT_EQ(batch.step, 16u);

    // The part's word of the new rate, and the first frame at it.
    grid.setOdr(kOdr25);
    grid.notBefore(18432);

    end   = 18461;
    batch = placeFrames(grid, kOdr25, true, 1, &end);

    EXPECT_EQ(batch.count, 1);
    EXPECT_EQ(batch.first, 18432u);
    EXPECT_EQ(grid.odr(), kOdr25);
}

// One frame in a short stretch fits several rates. For a stream nothing was
// placed of yet, the rate it had before the one written is the one it still
// runs at.
TEST(Bmi270FifoGrid, OneFrameOfAStreamJustJoinedIsAtTheRateItHadBefore)
{
    StreamGrid grid;
    grid.setOdr(kOdr50);
    grid.setOdr(kOdr100);
    grid.restart();

    const uint32_t from = 100u * 512u;
    grid.notBefore(from);

    const uint32_t          end   = from + 300u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 1, &end);

    EXPECT_EQ(batch.count, 1);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(grid.odr(), kOdr50);
}

// A rate written again after the part's word of the first one went into the
// FIFO and before it was read: by then the grid has the second rate, and the
// stretch between the two words was taken at the first.
TEST(Bmi270FifoGrid, AStretchBetweenTwoRateChangesIsPlacedAtItsOwnRate)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    uint32_t end = 100u * 2048u - 5u;
    ASSERT_EQ(placeFrames(grid, kOdr1600, true, 4, &end).count, 4);

    // The first word, for 25 Hz, taken in with 100 Hz already written.
    const uint32_t from = 100u * 2048u;
    grid.setOdr(kOdr100);
    grid.notBefore(from);

    // Two frames at 25 Hz before the second word.
    end = from + 1791u;

    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 2, &end);

    EXPECT_EQ(batch.count, 2);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(batch.step, 1024u);
}

// A change of rate as it usually goes: the stretch after the part's word is
// at the rate written, and is laid from where the word says.
TEST(Bmi270FifoGrid, AStretchAfterThePartsWordIsLaidFromWhereItSays)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    uint32_t end = 100u * 256u - 3u;
    ASSERT_EQ(placeFrames(grid, kOdr1600, true, 4, &end).count, 4);

    const uint32_t from = 100u * 256u;
    grid.setOdr(kOdr100);
    grid.notBefore(from);

    end = from + (4u * 256u) + 9u;

    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 5, &end);

    EXPECT_EQ(batch.count, 5);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(batch.step, 256u);
}

// Frames that fit no rate between the part's word and the end say nothing:
// the stream is laid as one that runs, back from the end at its own rate.
TEST(Bmi270FifoGrid, FramesThatFitNoRateLeaveTheStreamAtItsOwn)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    const uint32_t from = 100u * 256u;
    grid.notBefore(from);

    // In 100 counts: seven at 16, four at 32, two at 64. Never three.
    const uint32_t          end   = from + 100u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr1600, true, 3, &end);

    EXPECT_EQ(batch.count, 3);
    EXPECT_EQ(batch.step, 16u);
    EXPECT_EQ(lastOf(batch), from + 96u);
}

// ---------------------------------------------------------------------------
// A stretch the part dropped frames before
// ---------------------------------------------------------------------------

// The FIFO filled up, and the read did not reach its end either: nothing says
// where the frames that are left stand. Laid on from the last frame given
// out they would be early by what was dropped. They are left out, and the
// stream is given out again from the next read that has an end.
TEST(Bmi270FifoGrid, FramesAfterAnOverflowWithNoEndKnownAreLeftOut)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    uint32_t end = 200u * 256u;
    ASSERT_EQ(placeFrames(grid, kOdr100, true, 5, &end).count, 5);

    StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 6, nullptr, true);

    EXPECT_EQ(batch.count, 0);
    EXPECT_EQ(batch.dropped, 6);

    end   = 300u * 256u;
    batch = placeFrames(grid, kOdr100, true, 4, &end);

    EXPECT_EQ(batch.count, 4);
    EXPECT_EQ(lastOf(batch), 300u * 256u);

    // Without the part's word of frames dropped, the same batch is laid on.
    StreamGrid other;
    other.setOdr(kOdr100);
    end = 200u * 256u;
    ASSERT_EQ(placeFrames(other, kOdr100, true, 5, &end).count, 5);
    EXPECT_EQ(placeFrames(other, kOdr100, true, 6, nullptr).count, 6);
}

// With the end known, the frames left are counted back from it. Where the
// part said the stretch starts is of no use: the frames from there were the
// ones dropped, and those that came would pass for a slower rate.
TEST(Bmi270FifoGrid, FramesAfterAnOverflowAreCountedBackFromAKnownEnd)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    const uint32_t from = 100u * 1024u;
    grid.notBefore(from);

    // Two frames left of the eight that the stretch held.
    const uint32_t          end   = from + 2047u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr100, true, 2, &end, true);

    EXPECT_EQ(batch.count, 2);
    EXPECT_EQ(batch.step, 256u);
    EXPECT_EQ(lastOf(batch), from + 1792u);
}

// The same one frame in a short stretch, for a stream that runs on through a
// change of its rate: the rate it had before is not the one the frames after
// the part's word were taken at. The frames do not say, and the stream is
// laid at the rate set.
TEST(Bmi270FifoGrid, OneFrameOfARunningStreamIsNotTakenBackToTheRateBefore)
{
    StreamGrid grid;
    grid.setOdr(kOdr50);

    uint32_t end = 100u * 1024u - 3u;
    ASSERT_EQ(placeFrames(grid, kOdr50, true, 4, &end).count, 4);

    const uint32_t from = 100u * 1024u;
    grid.setOdr(kOdr25);
    grid.notBefore(from);

    end = from + 300u;

    const StreamGrid::Batch batch = placeFrames(grid, kOdr25, true, 1, &end);

    EXPECT_EQ(batch.count, 1);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(batch.step, 1024u);
    EXPECT_EQ(grid.odr(), kOdr25);
}

// With two streams the part's word names the next frame of either. The
// slower one's first frame after it stands later, on its own grid: its frames
// are counted on the multiples of its period from the word on, not from the
// word itself.
TEST(Bmi270FifoGrid, TheSlowerOfTwoStreamsIsCountedOnItsOwnGrid)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);
    grid.setOdr(kOdr25);
    grid.restart();

    // The faster stream's frame, a 1600 Hz period after a multiple of 256.
    const uint32_t from = (100u * 256u) + 16u;
    grid.notBefore(from);

    // Two frames of this stream by here: at 101 and 102 periods of 256.
    const uint32_t          end   = (102u * 256u) + 20u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr25, true, 2, &end);

    EXPECT_EQ(batch.count, 2);
    EXPECT_EQ(batch.first, 101u * 256u);
    EXPECT_EQ(batch.step, 256u);
}

// A stream still waiting for a slower rate stops some time before the read
// ends, and the part's word of the other stream's change names a start for
// both. Its last frames at the old rate are no measure of a rate then: two of
// them, with as long a silence after, would pass for half the rate. It goes
// on from its last frame at the rate it runs at.
TEST(Bmi270FifoGrid, AStreamWaitingForItsRateIsNotMeasuredByFramesThatStopShort)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    const uint32_t from = 100u * 1024u;

    uint32_t end = from - 3u;
    ASSERT_EQ(placeFrames(grid, kOdr100, true, 4, &end).count, 4);

    // The other stream's word; 25 Hz is written for this one and not yet in
    // effect.
    grid.notBefore(from);

    end = from + 600u;

    const StreamGrid::Batch batch = placeFrames(grid, kOdr25, true, 2, &end);

    EXPECT_EQ(batch.count, 2);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(batch.step, 256u);
    EXPECT_EQ(grid.odr(), kOdr100);
}

// The rate a stream had before it was switched off is not one it runs at
// when it is started again: one frame of it, which fits several rates, leaves
// it at the rate written.
TEST(Bmi270FifoGrid, AStreamStartedAgainIsNotTakenBackToTheRateItHadBeforeItStopped)
{
    StreamGrid grid;
    grid.setOdr(kOdr100);

    // Switched off, then set to 25 Hz while it does not run, and started.
    grid.setOdr(kOdr25, false);
    grid.restart();

    const uint32_t from = 100u * 1024u;
    grid.notBefore(from);

    const uint32_t          end   = from + 200u;
    const StreamGrid::Batch batch = placeFrames(grid, kOdr25, true, 1, &end);

    EXPECT_EQ(batch.count, 1);
    EXPECT_EQ(batch.first, from);
    EXPECT_EQ(grid.odr(), kOdr25);
}

// After an overflow the end of the read says where the frames left stand
// only for a stream that runs up to it. One that was switched off, or still
// waits for its new rate, stopped somewhere before: its frames have no place
// and are left out.
TEST(Bmi270FifoGrid, FramesAfterAnOverflowOfAStreamThatStoppedAreLeftOut)
{
    for (const bool live : { false, true }) {
        StreamGrid grid;
        grid.setOdr(kOdr100);

        uint32_t end = 200u * 256u;
        ASSERT_EQ(placeFrames(grid, kOdr100, true, 5, &end).count, 5);

        // Off, or on and waiting for 25 Hz.
        end = 300u * 256u;

        StreamGrid::Batch batch = placeFrames(grid, live ? kOdr25 : kOdr100, live, 3, &end, true);

        EXPECT_EQ(batch.count, 0) << "live " << live;
        EXPECT_EQ(batch.dropped, 3) << "live " << live;
    }
}

// A stretch said to end before where the part said it starts tells nothing
// of its rate, however many frames it holds.
TEST(Bmi270FifoGrid, AStretchThatEndsBeforeItStartsSaysNothingOfItsRate)
{
    StreamGrid grid;
    grid.setOdr(kOdr1600);

    const uint32_t end = (300u * 32768u) - 100u;

    grid.notBefore(300u * 32768u);

    // Round the whole counter from the start to the end, 512 frames would be
    // the slowest rate there is.
    const StreamGrid::Batch batch = placeFrames(grid, kOdr1600, true, 512, &end);

    EXPECT_EQ(batch.step, 16u);
    EXPECT_EQ(grid.odr(), kOdr1600);
}
