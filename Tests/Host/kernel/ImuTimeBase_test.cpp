/**
 ******************************************************************************
 * @file    ImuTimeBase_test.cpp
 * @brief   Host tests for the IMU's time base: frame intervals from the
 *          part's counter, a line to the system clock fitted to the reads,
 *          a rate for each of the part's two oscillators, and no frame later
 *          than its read. The part here is a counter running at a rate of
 *          its own and reads handed over late by a scatter that repeats, so
 *          every run is the same and none of it waits on real time.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "IMU/ImuTimeBase.hpp"

namespace {

constexpr double   kCountUs = 625.0 / 16.0;
constexpr uint32_t kMask    = ImuTimeBase::skCounterMask;

constexpr uint32_t k100Hz  = 256;    // counts a frame
constexpr uint32_t k25Hz   = 1024;
constexpr uint32_t k1600Hz = 16;

/// The two oscillators, as fractions the counter is slow by: about what a
/// part shows with the gyroscope off and on.
constexpr double kAccelClock = 0.0055;
constexpr double kGyroClock  = -0.0060;

/// One stream of frames out of the FIFO, with what the base said of every
/// frame and when the frame was really taken.
struct Stream {
    uint32_t              period = k100Hz;
    double                last   = -1.0;    // counter value of the last frame handed over
    std::vector<uint64_t> stamps;
    std::vector<double>   truth;
    std::vector<uint64_t> reads;

    explicit Stream(uint32_t counts) : period(counts) {}

    /// How far the time from a frame to the one so many frames on is off
    /// the true one, at its worst over frames [from, to), in parts per
    /// million.
    double worstPpm(size_t from, size_t to, size_t frames = 1) const
    {
        double worst = 0.0;

        for (size_t i = (from > frames) ? from : frames; (i < to) && (i < stamps.size()); i++) {
            const double took  = static_cast<double>(stamps[i] - stamps[i - frames]);
            const double truly = truth[i] - truth[i - frames];
            const double off   = std::fabs((took / truly) - 1.0) * 1e6;

            if (off > worst) {
                worst = off;
            }
        }

        return worst;
    }
};

/// The part and the thread that reads it: a counter running at a rate of
/// its own against the system clock, and reads handed over a few
/// milliseconds after the FIFO was emptied, on a clock that counts whole
/// milliseconds.
struct Rig {
    ImuTimeBase base;

    double clockUs = 5000000.0;
    double counts  = 4096.0;
    double rate    = kAccelClock;

    /// Where the oscillator was last changed, and the rate before it.
    double changedAtCounts = 0.0;
    double changedAtUs     = 0.0;
    double rateBefore      = kAccelClock;

    uint32_t seed    = 12345;
    double   evenUs  = -1.0;    // when not negative: every hand-over this long after its read
    uint64_t lastNow = 0;

    void run(double us)
    {
        clockUs += us;
        counts  += us / (kCountUs * (1.0 + rate));
    }

    /// Start a stream: its first frame is the next one on its grid.
    void start(Stream& stream) const
    {
        stream.last = std::floor(counts / stream.period) * stream.period;
    }

    void switchGyro(bool on)
    {
        changedAtCounts = counts;
        changedAtUs     = clockUs;
        rateBefore      = rate;
        rate            = on ? kGyroClock : kAccelClock;

        base.setGyroRunning(on, static_cast<uint64_t>(clockUs));
    }

    double takenAt(double count) const
    {
        if (count >= changedAtCounts) {
            return clockUs - ((counts - count) * kCountUs * (1.0 + rate));
        }

        return changedAtUs - ((changedAtCounts - count) * kCountUs * (1.0 + rateBefore));
    }

    /// 1 to 6 ms, the same sequence every run.
    double scatterUs()
    {
        if (evenUs >= 0.0) {
            return evenUs;
        }

        seed = (seed * 1103515245U) + 12345U;

        return 1000.0 + static_cast<double>((seed >> 8) % 5000U);
    }

    /// Empty the FIFO now and hand its batches over, in the order given.
    /// @param heldUpUs: How much later than usual the hand-over comes.
    void read(std::initializer_list<Stream*> streams, double heldUpUs = 0.0)
    {
        const double at = clockUs + scatterUs() + heldUpUs;

        lastNow = static_cast<uint64_t>(std::floor(at / 1000.0)) * 1000U;

        for (Stream* stream : streams) {
            const double newest = std::floor(counts / stream->period) * stream->period;

            if (newest <= stream->last) {
                continue;
            }

            base.observe(static_cast<uint32_t>(newest) & kMask, lastNow);

            for (double count = stream->last + stream->period; count <= newest; count += stream->period) {
                stream->stamps.push_back(base.stamp(static_cast<uint32_t>(count) & kMask, lastNow));
                stream->truth.push_back(takenAt(count));
                stream->reads.push_back(lastNow);
            }

            stream->last = newest;
        }
    }

    /// Run on to the next frame of a stream: the FIFO's level is what calls
    /// for a read, and it is a frame that brings it there.
    void runToFrame(const Stream& stream)
    {
        const double next = (std::floor(counts / stream.period) + 1.0) * stream.period;

        clockUs += (next - counts) * kCountUs * (1.0 + rate);
        counts   = next;
    }

    /// Reads of these streams, about so far apart, each called for by a
    /// frame of the fastest, and handed over in the order given.
    void reads(std::initializer_list<Stream*> streams, uint32_t howMany, double everyUs = 100000.0)
    {
        const Stream* fastest = *streams.begin();

        for (const Stream* stream : streams) {
            if (stream->period < fastest->period) {
                fastest = stream;
            }
        }

        for (uint32_t i = 0; i < howMany; i++) {
            run(everyUs);
            runToFrame(*fastest);
            read(streams);
        }
    }
};

void expectInOrderAndNotLate(const Stream& stream)
{
    for (size_t i = 0; i < stream.stamps.size(); i++) {
        EXPECT_LE(stream.stamps[i], stream.reads[i]) << "frame " << i << " is later than its read";

        if (i > 0U) {
            EXPECT_GT(stream.stamps[i], stream.stamps[i - 1]) << "frame " << i;
        }
    }
}

} // namespace

// Nothing is known yet of how late a hand-over comes: the newest frame of
// the first read is put the first lead under it, and the frames before it
// are counted back from there.
TEST(ImuTimeBase, TheFirstReadIsAnchoredUnderItsHandOver)
{
    ImuTimeBase base;

    base.observe(10000, 2000000);

    EXPECT_EQ(base.stamp(10000, 2000000), 2000000u - ImuTimeBase::skFirstLeadUs);
    EXPECT_EQ(base.stamp(10000 - k100Hz, 2000000), 2000000u - ImuTimeBase::skFirstLeadUs - 10000u);
}

// 256 counts a frame is 10 ms of the part's time and 10.055 ms of the
// system's when the counter is 0.55 % slow. Once the rate is fitted the
// frames are that far apart, within and across the reads, though every
// hand-over is 1 to 6 ms late on a clock that counts milliseconds.
TEST(ImuTimeBase, IntervalsFollowThePartsCounterAtItsFittedRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 200);

    // Worst seen here: 600 ppm, which is 6 us on a frame, in the reads the
    // line is still finding its place in.
    EXPECT_LT(accel.worstPpm(300, accel.stamps.size()), 900.0);
    EXPECT_LT(accel.worstPpm(1000, accel.stamps.size()), 400.0);

    const double took = static_cast<double>(accel.stamps.back() - accel.stamps[1000])
                      / static_cast<double>(accel.stamps.size() - 1001U);

    EXPECT_NEAR(took, 10000.0 * (1.0 + kAccelClock), 0.5);

    expectInOrderAndNotLate(accel);
}

// An oscillator the base has not seen is taken to run true, so the first
// reads are spaced by the part's tolerance at worst, and frames are squeezed
// where the line would pass a hand-over. Neither puts a frame after its read
// or behind the frame before it.
TEST(ImuTimeBase, AColdStartStaysInOrderAndUnderItsReads)
{
    for (const double clock : { kAccelClock, kGyroClock }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.rate       = clock;
        rig.rateBefore = clock;

        rig.start(accel);
        rig.reads({ &accel }, 40);

        expectInOrderAndNotLate(accel);

        // Worst seen here: 12300 ppm, a batch squeezed under its read.
        EXPECT_LT(accel.worstPpm(0, 200), 17000.0) << "clock " << clock;
    }
}

// The counter wraps every 655 s; a frame either side of the wrap is one
// period from the other.
TEST(ImuTimeBase, TheCounterWrappingIsNotVisible)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.counts = 16777216.0 - (60.0 * 2560.0);

    rig.start(accel);
    rig.reads({ &accel }, 200);

    ASSERT_GT(accel.last, 16777216.0);

    EXPECT_LT(accel.worstPpm(300, accel.stamps.size()), 900.0);
    expectInOrderAndNotLate(accel);
}

// The counter runs on another oscillator while the gyroscope runs, more than
// a per cent off the first. Each has a rate of its own, kept while the other
// is in use: back on an oscillator the base has seen, the frames are spaced
// right from the first read.
TEST(ImuTimeBase, EachOscillatorKeepsItsRateAcrossASwitch)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 150);

    rig.run(40000.0);
    rig.switchGyro(true);
    rig.reads({ &accel }, 150);

    for (uint32_t i = 0; i < 4; i++) {
        const size_t from = accel.stamps.size();

        // The first read after the switch is handed over later than most.
        rig.run(40000.0);
        rig.switchGyro((i % 2U) != 0U);
        rig.run(100000.0);
        rig.runToFrame(accel);
        rig.read({ &accel }, 4000.0);

        // Worst seen here: 2010 ppm, at the one frame the switch falls by,
        // which is placed to as near as the line is held under the
        // hand-overs. An oscillator's rate taken for the other's would be
        // 11500 ppm off on every frame.
        EXPECT_LT(accel.worstPpm(from, accel.stamps.size()), 3000.0) << "switch " << i;

        const size_t after = accel.stamps.size();

        rig.reads({ &accel }, 100);

        // How late the hand-overs come is known from before the switch, so
        // the new line does not take that first one for the earliest there
        // is and move up to it. Worst seen here: 300 ppm; with that first one taken for the earliest, 700 ppm.
        EXPECT_LT(accel.worstPpm(after, accel.stamps.size()), 450.0) << "switch " << i;
    }

    expectInOrderAndNotLate(accel);
}

// A read with fewer bytes in it is over sooner after its newest frame, as
// when the gyroscope's frames leave the FIFO. The hand-overs kept say nothing
// of that: the line has room under them for it, and no batch is squeezed
// under its read.
TEST(ImuTimeBase, AReadThatGotShorterSqueezesNothing)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);

    rig.evenUs = 3500.0;
    rig.reads({ &accel }, 150);

    const size_t from = accel.stamps.size();

    rig.evenUs = 1500.0;
    rig.reads({ &accel }, 100);

    // Worst seen here: 600 ppm; with no room for it, 15300 ppm.
    EXPECT_LT(accel.worstPpm(from, accel.stamps.size()), 900.0);
    expectInOrderAndNotLate(accel);
}

// Frames taken before the switch are still in the FIFO at the read after it.
// They keep the rate of the oscillator they were taken on, and those after
// the switch get the new one, in the one batch.
TEST(ImuTimeBase, FramesEitherSideOfASwitchGetTheirOwnRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 150);
    rig.run(40000.0);
    rig.switchGyro(true);
    rig.reads({ &accel }, 150);
    rig.run(40000.0);
    rig.switchGyro(false);
    rig.reads({ &accel }, 150, 1000000.0);

    // A second of frames a read, the switch in the middle of one.
    rig.run(500000.0);

    const size_t from = accel.stamps.size();

    rig.switchGyro(true);
    rig.run(500000.0);
    rig.runToFrame(accel);
    rig.read({ &accel });

    ASSERT_GT(accel.stamps.size() - from, 95U);

    // Worst seen here: 350 ppm.
    EXPECT_LT(accel.worstPpm(from + 1U, accel.stamps.size()), 1500.0);

    const double before = static_cast<double>(accel.stamps[from + 40U] - accel.stamps[from])
                        / (accel.truth[from + 40U] - accel.truth[from]);
    const double after  = static_cast<double>(accel.stamps.back() - accel.stamps[accel.stamps.size() - 41U])
                        / (accel.truth.back() - accel.truth[accel.truth.size() - 41U]);

    EXPECT_NEAR(before, 1.0, 0.0003);
    EXPECT_NEAR(after, 1.0, 0.0003);
}

// With two streams in the FIFO a read hands over a batch of each. The slower
// one's newest frame is older by up to its own period; taken for the read's
// newest it would pass for a hand-over that late. The base goes by the
// newest of the read whichever batch comes first, so both streams are
// spaced as a stream on its own is.
TEST(ImuTimeBase, TheSlowerStreamsBatchDoesNotPassForALateRead)
{
    for (const bool slowerFirst : { true, false }) {
        Rig    rig;
        Stream slow(k25Hz);
        Stream fast(k1600Hz);

        rig.start(slow);
        rig.start(fast);

        const auto reads = [&](uint32_t howMany) {
            if (slowerFirst) {
                rig.reads({ &slow, &fast }, howMany, 113000.0);
            } else {
                rig.reads({ &fast, &slow }, howMany, 113000.0);
            }
        };

        // The first read ties the counter to the clock at its newest frame,
        // though the slower stream's batch came first: the faster stream's
        // frames after that batch's newest are not squeezed in under the
        // hand-over. Worst seen here: 5500 ppm, the rate not yet fitted.
        reads(1);

        EXPECT_LT(fast.worstPpm(0, fast.stamps.size(), 16), 8000.0) << "slower first: " << slowerFirst;

        reads(9);

        // While the rate is still being fitted, a batch is put by the rate
        // as it was before its read, not by how late the batch itself came:
        // the slower stream's batch, handed over first and seeming late, is
        // on the line the read's newest frame then sets. Worst seen here:
        // 900 ppm; put by its own lateness, 5900 ppm.
        const size_t early = slow.stamps.size();

        reads(20);

        EXPECT_LT(slow.worstPpm(early, slow.stamps.size()), 2000.0) << "slower first: " << slowerFirst;

        reads(70);

        const size_t slowFrom = slow.stamps.size();
        const size_t fastFrom = fast.stamps.size();

        reads(200);

        // The faster stream's frames are 625 us apart, which whole
        // microseconds resolve to 1600 ppm: it is judged over 16 frames.
        // Worst seen here: 470 ppm for the slower stream and 200 ppm for
        // the faster.
        EXPECT_LT(slow.worstPpm(slowFrom, slow.stamps.size()), 700.0) << "slower first: " << slowerFirst;
        EXPECT_LT(fast.worstPpm(fastFrom, fast.stamps.size(), 16), 350.0) << "slower first: " << slowerFirst;

        const double took = static_cast<double>(fast.stamps.back() - fast.stamps[20000])
                          / (fast.truth.back() - fast.truth[20000]);

        EXPECT_NEAR(took, 1.0, 0.00005) << "slower first: " << slowerFirst;

        expectInOrderAndNotLate(slow);
        expectInOrderAndNotLate(fast);
    }
}

// A read held up for 35 ms says nothing of the oscillator. It is left out
// of the fit, so the frames after it stay spaced as before.
TEST(ImuTimeBase, AHeldUpReadDoesNotMoveTheRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 200);

    const size_t from = accel.stamps.size();

    rig.run(100000.0);
    rig.runToFrame(accel);
    rig.read({ &accel }, 35000.0);
    rig.reads({ &accel }, 60);

    // Worst seen here: 200 ppm.
    EXPECT_LT(accel.worstPpm(from, accel.stamps.size()), 400.0);
    expectInOrderAndNotLate(accel);
}

// The first reads of an oscillator the base has not seen are all the fit
// has, and one held up among them would pass for an oscillator several per
// cent off.
TEST(ImuTimeBase, AHeldUpReadAmongTheFirstDoesNotMoveTheRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 6);

    rig.run(100000.0);
    rig.runToFrame(accel);
    rig.read({ &accel }, 35000.0);

    const size_t from = accel.stamps.size();

    rig.reads({ &accel }, 30);

    // Worst seen here: 1400 ppm, the line still finding its place; with the
    // read in the fit, 18000 ppm.
    EXPECT_LT(accel.worstPpm(from + 10U, accel.stamps.size()), 3000.0);
    expectInOrderAndNotLate(accel);
}

// A read handed over in the millisecond the gyroscope was switched in: its
// newest frame, counted on from the switch, would be later than the read,
// and a whole lead under the read is behind the switch. It stays where the
// switch is.
TEST(ImuTimeBase, AReferenceIsNotSqueezedBehindTheOneBeforeIt)
{
    ImuTimeBase base;

    base.observe(10000, 2000000);
    base.setGyroRunning(true, 2000500);
    base.observe(10200, 2000600);

    const uint64_t before = base.stamp(10050, 2000600);
    const uint64_t at     = base.stamp(10089, 2000600);
    const uint64_t after  = base.stamp(10150, 2000600);
    const uint64_t newest = base.stamp(10200, 2000600);

    EXPECT_LT(before, at);
    EXPECT_LE(at, after);
    EXPECT_LE(after, newest);
    EXPECT_LE(newest, 2000600u);
}

// A hand-over that comes earlier than any before it would leave the line
// above it. The read's frames are squeezed under it: none is later than the
// read and none is behind another.
TEST(ImuTimeBase, ABatchIsSqueezedUnderAnEarlyHandOver)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);

    // Every hand-over 20 ms late, then one that is not.
    for (uint32_t i = 0; i < 50; i++) {
        rig.run(100000.0);
        rig.runToFrame(accel);
        rig.read({ &accel }, 20000.0);
    }

    const size_t from = accel.stamps.size();

    rig.run(100000.0);
    rig.runToFrame(accel);
    rig.read({ &accel });

    EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skLeadUs);
    EXPECT_LT(accel.stamps.back() - accel.stamps[from - 1U], 95000U);

    expectInOrderAndNotLate(accel);
}

// A stop unties the counter from the system clock, so the next read is
// anchored like a first one; the oscillator is the same, so its rate is
// there from the first read.
TEST(ImuTimeBase, AResetAnchorsAnewAndKeepsTheRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 200);

    rig.base.reset();
    rig.run(30000000.0);
    rig.start(accel);

    const size_t from = accel.stamps.size();

    rig.reads({ &accel }, 1);

    EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skFirstLeadUs);

    rig.reads({ &accel }, 30);

    // Worst seen here: 500 ppm.
    EXPECT_LT(accel.worstPpm(from + 1U, accel.stamps.size()), 800.0);
}

// The counter wraps every 655 s, so well before half of that it stops being
// taken as saying how long it has been. A read within the limit goes on from
// the line; one past it is anchored anew.
TEST(ImuTimeBase, AReadLongAfterTheLastAnchorsAnew)
{
    const double limitUs = static_cast<double>(ImuTimeBase::skLongestGapUs);

    for (const double pauseUs : { limitUs - 10.0e6, limitUs + 10.0e6 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.start(accel);
        rig.reads({ &accel }, 200);

        rig.run(pauseUs);
        rig.start(accel);
        rig.reads({ &accel }, 1);

        const bool anchored = (accel.stamps.back() == rig.lastNow - ImuTimeBase::skFirstLeadUs);

        EXPECT_EQ(anchored, pauseUs > limitUs) << "pause " << pauseUs;

        // On from the line, the frame is where the part took it: the rate
        // held over the pause.
        if (pauseUs < limitUs) {
            EXPECT_NEAR(static_cast<double>(accel.stamps.back()), accel.truth.back(), 3000.0);
        }
    }
}

// Reads a second apart, as with no latency asked for: the line runs on from
// read to read, so the step from one read's last frame into the next read is
// a period like any other.
TEST(ImuTimeBase, ReadsASecondApartRunOnFromOneAnother)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 120, 1000000.0);

    // Worst seen here: 100 ppm.
    EXPECT_LT(accel.worstPpm(3000, accel.stamps.size()), 400.0);
    expectInOrderAndNotLate(accel);
}

// The system clock steps on. A read far ahead of the line may only have been
// held up, so the first one after the step still stands on the old line; the
// second, as far ahead again, is anchored on the clock as it is now. A step
// too small to tell from a late read is left to the line's slow move.
TEST(ImuTimeBase, AStepOnOfTheSystemClockAnchorsAnewAtTheSecondRead)
{
    for (const double step : { 1.5e6, 5.0e6, 30.0e6 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.start(accel);
        rig.reads({ &accel }, 50);

        rig.clockUs     += step;
        rig.changedAtUs += step;

        rig.reads({ &accel }, 1);
        EXPECT_LT(accel.stamps.back(), rig.lastNow - static_cast<uint64_t>(step) + 10000U) << "step " << step;

        rig.reads({ &accel }, 1);
        EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skFirstLeadUs) << "step " << step;

        // A read held up right after that is one read ahead of the new
        // line, not the second in a row: it stays on the line.
        rig.run(100000.0);
        rig.runToFrame(accel);
        rig.read({ &accel }, 1200000.0);
        EXPECT_LT(accel.stamps.back(), rig.lastNow - 1000000U) << "step " << step;
    }

    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 50);

    rig.clockUs     += 0.5e6;
    rig.changedAtUs += 0.5e6;

    rig.reads({ &accel }, 5);

    EXPECT_LT(accel.stamps.back(), rig.lastNow - 400000U);
}

// The system clock goes back, or steps on by more than the counter can
// vouch for: the next read is anchored anew at once.
TEST(ImuTimeBase, AStepBackOfTheSystemClockAnchorsAnewAtOnce)
{
    for (const double step : { 3600.0e6, -4.0e6 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.start(accel);
        rig.reads({ &accel }, 50);

        rig.clockUs     += step;
        rig.changedAtUs += step;

        rig.reads({ &accel }, 1);

        EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skFirstLeadUs) << "step " << step;
    }

    // Reads two seconds apart, so that a step back by less than that leaves
    // the hand-overs in order: over a second behind the line is a step, and
    // half a second is a batch squeezed under its read.
    for (const double step : { -1.5e6, -0.5e6 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.start(accel);
        rig.reads({ &accel }, 30, 2000000.0);

        rig.clockUs     += step;
        rig.changedAtUs += step;

        rig.reads({ &accel }, 1, 2000000.0);

        const uint64_t lead = (step < -1.0e6) ? ImuTimeBase::skFirstLeadUs : ImuTimeBase::skLeadUs;

        EXPECT_EQ(accel.stamps.back(), rig.lastNow - lead) << "step " << step;
    }
}

// A read held up for over a second, as by a long hold of the bus or of the
// thread, says nothing of the clock. The line stays: the frames of that read
// and of the reads after it are where the part took them.
TEST(ImuTimeBase, AReadHeldUpForOverASecondDoesNotMoveTheLine)
{
    for (const double heldUs : { 900000.0, 1200000.0, 5000000.0 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.start(accel);
        rig.reads({ &accel }, 200);

        const size_t from = accel.stamps.size();

        rig.run(100000.0);
        rig.runToFrame(accel);
        rig.read({ &accel }, heldUs);

        // The part went on taking frames while the read was held, and the
        // next read comes after it.
        rig.run(heldUs);
        rig.reads({ &accel }, 60);

        // Worst seen here: 200 ppm.
        EXPECT_LT(accel.worstPpm(from, accel.stamps.size()), 400.0) << "held " << heldUs;

        for (size_t i = from; i < accel.stamps.size(); i++) {
            ASSERT_NEAR(static_cast<double>(accel.stamps[i]), accel.truth[i], 3000.0) << "held " << heldUs;
        }

        expectInOrderAndNotLate(accel);
    }
}

// An oscillator not seen yet, as far off as the part may be, and reads far
// apart: the counter has run seconds ahead of what the clock says by the
// first of them. That is the part's tolerance, not a step of the clock, and
// the line is kept for the fit to find the rate.
TEST(ImuTimeBase, ALongStepAtAnUnknownRateIsNotAStepOfTheClock)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.rate       = -0.017;
    rig.rateBefore = -0.017;

    rig.start(accel);
    rig.reads({ &accel }, 2, 100.0e6);

    EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skLeadUs);
}

// A frame newer than any batch taken in runs on from the last one at the
// fitted rate, and still is not later than the read it is asked for.
TEST(ImuTimeBase, AFramePastTheLastBatchRunsOnAtTheFittedRate)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 200);

    const uint32_t newest = static_cast<uint32_t>(accel.last) & kMask;
    const uint64_t last   = accel.stamps.back();

    EXPECT_NEAR(static_cast<double>(rig.base.stamp(newest + k100Hz, last + 1000000U) - last),
                10000.0 * (1.0 + kAccelClock), 2.0);
    EXPECT_EQ(rig.base.stamp(newest + k100Hz, last + 5000U), last + 5000U);
}

// A batch whose newest frame is older than one taken in already - a stream
// that stopped before the read - changes nothing.
TEST(ImuTimeBase, AnOlderBatchChangesNothing)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 50);

    const uint32_t newest = static_cast<uint32_t>(accel.last) & kMask;
    const uint64_t before = rig.base.stamp(newest, UINT64_MAX);

    rig.base.observe(newest - k25Hz, rig.lastNow + 100000U);

    EXPECT_EQ(rig.base.stamp(newest, UINT64_MAX), before);
    EXPECT_EQ(rig.base.stamp(newest - k100Hz, UINT64_MAX), accel.stamps[accel.stamps.size() - 2U]);

    // Nor does the same frame handed over again.
    rig.base.observe(newest, rig.lastNow + 200000U);

    EXPECT_EQ(rig.base.stamp(newest, UINT64_MAX), before);
    EXPECT_EQ(rig.base.stamp(newest - k100Hz, UINT64_MAX), accel.stamps[accel.stamps.size() - 2U]);
    EXPECT_EQ(rig.base.stamp(newest + k100Hz, UINT64_MAX) - before,
              rig.base.stamp(newest + 2U * k100Hz, UINT64_MAX) - rig.base.stamp(newest + k100Hz, UINT64_MAX));

    // Nor does one far behind in the same read: a slow stream's batch, a
    // period of its own old, handed over after a faster one's.
    rig.base.observe(newest + k100Hz, rig.lastNow + 150000U);

    const uint64_t later = rig.base.stamp(newest + k100Hz, UINT64_MAX);

    rig.base.observe((newest + k100Hz - 30000U) & kMask, rig.lastNow + 150000U);

    EXPECT_EQ(rig.base.stamp(newest + k100Hz, UINT64_MAX), later);
}

// The part was reset behind the base's back and its counter started anew.
// The next read's frames stand far behind the last ones by the counter, or,
// from the upper half of its range, far ahead of them. Either way the read
// is anchored anew at once.
TEST(ImuTimeBase, ACounterThatStartedAnewAnchorsAnew)
{
    for (const double counts : { 2000000.0, 12000000.0 }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.counts = counts;
        rig.start(accel);
        rig.reads({ &accel }, 50);

        rig.counts = 256.0;
        rig.start(accel);
        rig.reads({ &accel }, 1);

        EXPECT_EQ(accel.stamps.back(), rig.lastNow - ImuTimeBase::skFirstLeadUs) << "from " << counts;

        rig.reads({ &accel }, 20);

        // The line runs on from there. Worst seen here: 400 ppm.
        EXPECT_LT(accel.worstPpm(accel.stamps.size() - 150U, accel.stamps.size()), 800.0) << "from " << counts;
    }
}

// The reference set at a switch of the gyroscope stands ahead of the frames
// still in the FIFO, by as long as they have waited there: at the slowest
// rate, more than a second. Such a frame is not a counter that started anew.
TEST(ImuTimeBase, AFrameThatWaitedThroughASwitchIsNotACounterStartedAnew)
{
    constexpr uint32_t kSlowest = 32768;

    Rig    rig;
    Stream accel(kSlowest);

    rig.start(accel);
    rig.reads({ &accel }, 20, 1000000.0);

    // A frame comes, the gyroscope is switched on 1.17 s later, and the
    // read follows.
    rig.runToFrame(accel);
    rig.run(1170000.0);
    rig.switchGyro(true);
    rig.run(20000.0);
    rig.read({ &accel });

    // The frame follows the one before it at their true distance. Worst seen
    // here: 40 ppm. Anchored at its read it would come 1.19 s late.
    EXPECT_LT(accel.worstPpm(accel.stamps.size() - 1U, accel.stamps.size()), 200.0);
}

// How far behind is far enough: nine seconds of counts change nothing, eleven
// are a counter that started anew.
TEST(ImuTimeBase, ACounterStartedAnewIsToldByTenSecondsOfCounts)
{
    for (const uint32_t behind : { 230400U, 281600U }) {
        Rig    rig;
        Stream accel(k100Hz);

        rig.counts = 2000000.0;
        rig.start(accel);
        rig.reads({ &accel }, 50);

        const uint32_t newest = static_cast<uint32_t>(accel.last) & kMask;
        const uint64_t before = rig.base.stamp(newest, UINT64_MAX);
        const uint64_t now    = rig.lastNow + 100000U;

        rig.base.observe(newest - behind, now);

        if (behind < ImuTimeBase::skRestartCounts) {
            EXPECT_EQ(rig.base.stamp(newest, UINT64_MAX), before);
        } else {
            EXPECT_EQ(rig.base.stamp(newest - behind, now), now - ImuTimeBase::skFirstLeadUs);
        }
    }
}

// On a clock finer than a millisecond two reads can come within the lead of
// one another. The second, squeezed a lead under its own hand-over, would
// stand before the first: it is held to it.
TEST(ImuTimeBase, ASqueezedReadIsNotPutBeforeTheOneBeforeIt)
{
    Rig    rig;
    Stream accel(k100Hz);

    rig.start(accel);
    rig.reads({ &accel }, 50);

    const uint32_t newest = static_cast<uint32_t>(accel.last) & kMask;

    // A read handed over just where the line puts its newest frame, then
    // one half a millisecond on.
    const uint64_t onLine = rig.base.stamp(newest + k100Hz, UINT64_MAX);

    rig.base.observe(newest + k100Hz, onLine);

    const uint64_t first = rig.base.stamp(newest + k100Hz, UINT64_MAX);

    rig.base.observe(newest + 2U * k100Hz, onLine + 500U);

    EXPECT_GE(rig.base.stamp(newest + 2U * k100Hz, UINT64_MAX), first);
    EXPECT_LE(rig.base.stamp(newest + 2U * k100Hz, onLine + 500U), onLine + 500U);
}
