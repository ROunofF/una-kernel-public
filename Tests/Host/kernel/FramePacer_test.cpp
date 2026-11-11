/**
 * Host unit tests for FramePacer -- what decides when the GUI is handed a frame
 * on a screen that keeps no beat of its own.
 *
 * The reason to pin this down is the shape of its failure. A claim that lands
 * in the wrong place does not draw a wrong frame; it draws nothing, and goes on
 * drawing nothing, because the count and the timer have come apart and nothing
 * afterwards puts them back together. The interleavings around the stop are
 * therefore the point of these tests, more than the ordinary counting.
 *
 * The pacer performs its effects itself, so the tests read them back from a
 * recorder rather than from return values.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "FramePacer.hpp"

namespace {

constexpr uint32_t kRunFrames = 10;

/// Records what the pacer asked for, in the order it asked.
class Recorder {

public:

    void signalFrame() { mLog.push_back("frame"); }
    void startTimer()  { mLog.push_back("start"); }
    void stopTimer()   { mLog.push_back("stop"); }

    const std::vector<std::string>& log() const { return mLog; }

    size_t count(const char *what) const
    {
        size_t n = 0;
        for (const std::string &entry : mLog) {
            if (entry == what) {
                ++n;
            }
        }
        return n;
    }

    void clear() { mLog.clear(); }

private:

    std::vector<std::string> mLog;
};

/// A recorder whose stopTimer() lets the test act in the instant after the stop
/// and before the pacer looks again -- the window the design is built around.
class StopHookRecorder : public Recorder {

public:

    void stopTimer()
    {
        Recorder::stopTimer();
        if (mOnStop) {
            auto hook = mOnStop;
            mOnStop = nullptr;   // once only, or a claim would recurse
            hook();
        }
    }

    void onStop(std::function<void()> hook) { mOnStop = std::move(hook); }

private:

    std::function<void()> mOnStop;
};

using Pacer = FramePacer<Recorder>;

/// Lets the run that entering the on-demand mode starts play out, so what
/// follows begins from a quiet pacer rather than mid-run.
template <class P>
void settle(P &pacer)
{
    while (!pacer.onPass()) {
        // Entering the mode owes a full run: whatever was in hand when the beat
        // stopped still has to be drawn.
    }
}

/// A pacer in the on-demand mode with nothing owed, which is where all of this
/// applies.
class OnDemand {

public:

    OnDemand()
        : mPacer(mFx, kRunFrames)
    {
        mPacer.setLowRate(true);
        settle(mPacer);
        mFx.clear();
    }

    Recorder mFx;
    Pacer    mPacer;
};

} // namespace

// -- The ordinary rate -------------------------------------------------------

TEST(FramePacer, EveryPassIsAFrameAtTheOrdinaryRate)
{
    Recorder fx;
    Pacer pacer(fx, kRunFrames);   // starts at the ordinary rate

    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(pacer.onPass());
    }

    EXPECT_EQ(fx.count("frame"), 5u);
    EXPECT_EQ(fx.count("stop"), 0u);
    EXPECT_EQ(pacer.owed(), 0u);
}

TEST(FramePacer, ClaimsAreIgnoredAtTheOrdinaryRate)
{
    Recorder fx;
    Pacer pacer(fx, kRunFrames);

    EXPECT_FALSE(pacer.forceRun());
    EXPECT_FALSE(pacer.requestOne());

    EXPECT_TRUE(fx.log().empty());
    EXPECT_EQ(pacer.owed(), 0u);
}

// -- A run -------------------------------------------------------------------

TEST(FramePacer, ARunHandsOutItsFramesAndOneSilentPass)
{
    OnDemand d;

    EXPECT_TRUE(d.mPacer.forceRun());
    EXPECT_EQ(d.mFx.count("start"), 1u);

    // Every pass but the last hands out a frame.
    for (uint32_t i = 0; i < kRunFrames; ++i) {
        EXPECT_FALSE(d.mPacer.onPass()) << "pass " << i;
    }
    EXPECT_EQ(d.mFx.count("frame"), kRunFrames);
    EXPECT_EQ(d.mFx.count("stop"), 0u);

    // And the one after it hands out nothing and stops.
    EXPECT_TRUE(d.mPacer.onPass());
    EXPECT_EQ(d.mFx.count("frame"), kRunFrames);
    EXPECT_EQ(d.mFx.count("stop"), 1u);
    EXPECT_EQ(d.mPacer.owed(), 0u);
}

TEST(FramePacer, ASecondRunDoesNotStartTheTimerAgain)
{
    OnDemand d;

    EXPECT_TRUE(d.mPacer.forceRun());
    d.mPacer.onPass();

    // The timer is already going, so this only refills the count.
    EXPECT_FALSE(d.mPacer.forceRun());
    EXPECT_EQ(d.mFx.count("start"), 1u);
    EXPECT_EQ(d.mPacer.owed(), kRunFrames + 1);
}

// -- A single frame ----------------------------------------------------------

TEST(FramePacer, OneRequestGivesOneFrameThenStops)
{
    OnDemand d;

    EXPECT_TRUE(d.mPacer.requestOne());
    EXPECT_EQ(d.mFx.count("start"), 1u);

    EXPECT_FALSE(d.mPacer.onPass());
    EXPECT_EQ(d.mFx.count("frame"), 1u);

    EXPECT_TRUE(d.mPacer.onPass());
    EXPECT_EQ(d.mFx.count("frame"), 1u);
    EXPECT_EQ(d.mFx.count("stop"), 1u);
}

TEST(FramePacer, ARequestDuringARunDoesNotShortenIt)
{
    OnDemand d;

    d.mPacer.forceRun();
    d.mPacer.onPass();               // one frame handed out, ten passes left

    EXPECT_FALSE(d.mPacer.requestOne());
    EXPECT_EQ(d.mPacer.owed(), kRunFrames)
            << "raised to two rather than set to it, so the run stands";
}

TEST(FramePacer, ARequestOnTheLastPassStillBuysAFrame)
{
    OnDemand d;

    // Down to the pass that hands out nothing.
    d.mPacer.requestOne();
    d.mPacer.onPass();
    ASSERT_EQ(d.mPacer.owed(), 1u);

    // A count of one is not left alone: that pass may be the silent one, or a
    // frame already past the point where it would have read the surface.
    EXPECT_FALSE(d.mPacer.requestOne());
    EXPECT_EQ(d.mPacer.owed(), 2u);

    d.mFx.clear();
    EXPECT_FALSE(d.mPacer.onPass());
    EXPECT_EQ(d.mFx.count("frame"), 1u);
}

// -- The window the whole design is built around -----------------------------

TEST(FramePacer, AClaimLandingInTheStopWindowRestartsTheTimer)
{
    StopHookRecorder fx;
    FramePacer<StopHookRecorder> pacer(fx, kRunFrames);
    pacer.setLowRate(true);
    settle(pacer);
    pacer.requestOne();
    pacer.onPass();                  // the frame; next pass is the silent one
    fx.clear();

    // A claim arriving after the stop is on its way. Without the look that
    // follows the stop, it would hold a count with no timer to spend it, and
    // every later claim would see a non-zero count and leave the starting to
    // somebody else.
    fx.onStop([&pacer] { pacer.requestOne(); });

    EXPECT_FALSE(pacer.onPass()) << "the run did not end: a claim came in";
    EXPECT_EQ(fx.count("stop"), 1u);
    EXPECT_GE(fx.count("start"), 1u) << "the timer was not left stopped";
    EXPECT_GT(pacer.owed(), 0u);

    // Two starts, not one: the claim found a count of zero and started the
    // timer itself, and the look after the stop started it again. Neither knows
    // about the other, and neither can be dropped -- the claim cannot tell it
    // arrived inside a stop, and the look cannot tell a claim has already acted.
    // Starting a periodic timer that is already running only re-phases it, so
    // the belt and braces cost a fraction of a period and nothing else.
}

TEST(FramePacer, ReturningToTheOrdinaryRateInTheStopWindowKeepsTheTimer)
{
    StopHookRecorder fx;
    FramePacer<StopHookRecorder> pacer(fx, kRunFrames);
    pacer.setLowRate(true);
    settle(pacer);
    pacer.requestOne();
    pacer.onPass();
    fx.clear();

    // The worse of the two: the beat that the mode change starts is the one
    // being stopped here. Miss this and nothing in the ordinary mode ever asks
    // for a frame again -- the display never draws.
    fx.onStop([&pacer] { pacer.setLowRate(false); });

    EXPECT_FALSE(pacer.onPass());
    EXPECT_EQ(fx.count("stop"), 1u);
    EXPECT_GE(fx.count("start"), 1u) << "the ordinary beat survived the stop";
}

// -- Switching modes ---------------------------------------------------------

TEST(FramePacer, LeavingTheOrdinaryRateFinishesWhatWasInHand)
{
    Recorder fx;
    Pacer pacer(fx, kRunFrames);

    EXPECT_TRUE(pacer.setLowRate(true));
    EXPECT_EQ(pacer.owed(), kRunFrames + 1)
            << "whatever was in hand when the beat stopped still has to be drawn";
    EXPECT_EQ(fx.count("start"), 1u);
}

TEST(FramePacer, ReturningToTheOrdinaryRateStartsTheBeat)
{
    OnDemand d;

    EXPECT_TRUE(d.mPacer.setLowRate(false));
    EXPECT_EQ(d.mFx.count("start"), 1u);

    // And from here the count is not consulted at all.
    d.mFx.clear();
    d.mPacer.onPass();
    EXPECT_EQ(d.mFx.count("frame"), 1u);
}

TEST(FramePacer, SayingTheSameRateTwiceDoesNothing)
{
    OnDemand d;

    EXPECT_FALSE(d.mPacer.setLowRate(true));
    EXPECT_TRUE(d.mFx.log().empty());
}
