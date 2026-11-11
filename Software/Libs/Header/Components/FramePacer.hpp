/**
 ******************************************************************************
 * @file    FramePacer.hpp
 * @brief   Deciding when the GUI is handed a frame, on a screen with no beat.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_FRAME_PACER_HPP
#define COMPONENT_FRAME_PACER_HPP

#include <atomic>
#include <cstdint>

/**
 * @brief   When the GUI is handed a frame.
 *
 *          Frames are produced in one of two ways.
 *
 *          While the GUI has something of its own to draw, the timer runs and
 *          every pass hands out a frame. That is the ordinary rate, and none of
 *          what follows applies to it.
 *
 *          While it has not -- a screen that changes only when something tells
 *          it to -- the timer is quiet and frames are handed out on demand.
 *          Demand comes in two kinds, and they are not the same thing:
 *
 *            A run, for work whose extent cannot be known from outside. An
 *            event may change the screen, and a screen change is carried out on
 *            the tick after the one that asks for it; a key may start an
 *            animation; several events may be waiting at once. Enumerating
 *            those cases is a losing game, so a run simply lets the GUI go on
 *            for a while, which covers all of them and any yet to be invented.
 *
 *            A single frame, for work whose extent is exactly one: an app has
 *            put a frame on the shared surface, and carrying it to the panel is
 *            the whole of what follows.
 *
 *          What is counted is passes of the timer, not frames, and one pass
 *          more than the frames wanted is always asked for. That last pass
 *          hands out nothing. It is there so the count reaches zero a whole
 *          period after the last frame rather than at the same moment, which
 *          leaves a period-wide window for a claim to land in: within it there
 *          is already a pass coming, and the claim is carried by that one
 *          instead of having to start the timer itself. What the window
 *          replaces is a knife edge between "the last frame has gone out" and
 *          "the timer is stopping", where a claim would have found a count it
 *          could not spend.
 *
 *          Every frame therefore reaches the GUI from a pass of the timer, and
 *          passes are a period apart. That is not incidental. Handing a frame
 *          over does not queue it: the hand-off itself always works -- it is a
 *          single slot, safe to fill from any thread at any time -- but the GUI
 *          empties that slot on its way back to waiting, which is to say the
 *          instant its render ends. A frame offered at any point between the
 *          start of a frame and the end of its drawing is therefore thrown away
 *          exactly where it would otherwise have been picked up, with no way to
 *          tell that it was and nothing afterwards to make up for it.
 *
 *          That clearing is deliberate on the framework's part -- a render that
 *          overruns should skip the frame it missed rather than drag a backlog
 *          behind it -- and it is not ours to change. Keeping a period between
 *          frames is what stays clear of it, and it is why nothing here hands
 *          the GUI a frame directly, however idle it looks: two hand-offs in
 *          quick succession would put the second inside the render started by
 *          the first.
 *
 *          All of which rests on a frame being finished within a period. The
 *          drawing itself is quick; the frame is not on the panel until it has
 *          been clocked out, and that is what takes the time -- tens of
 *          milliseconds, well inside a period as things stand. Should a screen
 *          ever take longer than one, the window above would still be open when
 *          the next pass came, and frames would begin to be dropped again.
 *
 *          Four threads reach this: the timer daemon, the GUI, an app's own
 *          thread by way of the surface, and the backend. Nothing is locked --
 *          the count is the only shared state and every change to it is a
 *          single atomic operation.
 *
 * @tparam  Effects  What to do, once it has been decided. Three methods, all
 *                   called from inside this class so that no decision is ever
 *                   separated from what it decides:
 *                     signalFrame() -- hand the GUI a frame
 *                     startTimer()  -- start the periodic timer
 *                     stopTimer()   -- stop it
 *                   Given as a template parameter rather than an interface so
 *                   the firmware pays nothing for the indirection and a test
 *                   can record the order the three arrive in.
 */
template <class Effects>
class FramePacer {

public:

    /**
     * @param   fx: performs the three effects; outlives this.
     * @param   runFrames: frames a run hands out, not counting the pass that
     *          ends it.
     */
    FramePacer(Effects &fx, uint32_t runFrames)
        : mFx(fx)
        , mRunFrames(runFrames)
    {
    }

    /**
     * @brief   A pass of the timer.
     * @retval  true when this pass ended a run, which is worth a word in the
     *          log and nothing else.
     */
    bool onPass()
    {
        if (!mLowRate.load()) {
            // The ordinary rate: every pass is a frame and the count is not
            // part of it.
            mFx.signalFrame();
            return false;
        }

        uint32_t left = mOwed.load();
        while (left > 0 && !mOwed.compare_exchange_weak(left, left - 1)) {
            // Retry: the count moved between the read and the write.
        }

        if (left > 1) {
            mFx.signalFrame();
            return false;
        }

        // The last pass, which hands out nothing.
        mFx.stopTimer();

        // Both of the things that would have wanted this timer are looked at
        // again rather than reasoned about, because stopping is the one thing
        // here that cannot be recalled once it is on its way. A claim made
        // between the count reaching zero and the stop would be left with a
        // count and no timer to spend it, and every claim after it would see a
        // count that is not zero and leave the starting to somebody else. A
        // return to the ordinary rate in that same gap is worse: the beat it
        // started is stopped here, nothing in that mode ever asks for a frame,
        // and the display never draws again.
        //
        // This is why the stop and the look that follows it are in one place
        // and not split across a caller.
        if (mOwed.load() > 0 || !mLowRate.load()) {
            mFx.startTimer();
            return false;
        }

        return true;
    }

    /**
     * @brief   Say whether the screen now up keeps a beat of its own.
     * @param   enable: true when it does not, and frames are on demand.
     * @retval  true on a change, which is all the caller has to say about.
     */
    bool setLowRate(bool enable)
    {
        if (mLowRate.load() == enable) {
            return false;
        }

        mLowRate.store(enable);

        if (enable) {
            // Whatever was in hand when the beat stopped still has to be
            // finished.
            forceRun();
        } else {
            mFx.startTimer();
        }

        return true;
    }

    /**
     * @brief   Let the GUI run on for a while.
     * @retval  true if this claim was the one that set the timer going.
     */
    bool forceRun()
    {
        if (!mLowRate.load()) {
            return false;
        }

        // One pass more than the frames wanted, for the one that hands out
        // nothing. Nothing owed before this means the timer is quiet, so this
        // claim has to set it going.
        if (mOwed.exchange(mRunFrames + 1) != 0) {
            return false;
        }

        mFx.startTimer();
        return true;
    }

    /**
     * @brief   Ask for exactly one frame.
     * @retval  true if this claim was the one that set the timer going.
     */
    bool requestOne()
    {
        if (!mLowRate.load()) {
            return false;
        }

        // Two passes: one to hand out the frame and one to end the run. Raised
        // to that rather than set, so a run already under way is left as it is
        // -- its frames read the surface on their way and this is carried by
        // one of them.
        //
        // A count of one is not enough to leave alone. That pass may be the one
        // that hands out nothing, and it may equally be a frame already under
        // way and past the point where it would have read the surface.
        uint32_t owed = mOwed.load();
        while (owed < 2 && !mOwed.compare_exchange_weak(owed, 2)) {
            // Retry: the count moved between the read and the write.
        }

        // Nothing owed before this means the timer is quiet, so it is this
        // claim that has to set it going.
        if (owed != 0) {
            return false;
        }

        mFx.startTimer();
        return true;
    }

    /// Passes still owed. For a test to look at; nothing decides on it.
    uint32_t owed() const { return mOwed.load(); }

private:

    Effects              &mFx;
    const uint32_t        mRunFrames;
    std::atomic<uint32_t> mOwed { 0 };
    std::atomic<bool>     mLowRate { false };
};

#endif // COMPONENT_FRAME_PACER_HPP
