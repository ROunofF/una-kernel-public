/**
 ******************************************************************************
 * @file    BaroCadence.hpp
 * @brief   When a barometer with no clock of its own measures: the periods on
 *          offer, and the instants a stream at one of them falls due.
 *
 *          Arithmetic only: no clock, bus or thread, so it can be checked
 *          without a part or an RTOS.
 *
 *          Times are the millisecond clock, compared modulo 2^32. That clock
 *          wraps about every 48.5 days and not at 2^32, so across its wrap a
 *          deadline reads as long overdue: one measurement comes up to a
 *          period early, and the grid starts over from it.
 ******************************************************************************
 */

#ifndef COMPONENT_BARO_CADENCE_HPP
#define COMPONENT_BARO_CADENCE_HPP

#include <cstddef>
#include <cstdint>

/**
 * @brief   The periods on offer, and the grid a stream at one of them keeps.
 */
class BaroCadence
{

public:

    /// The periods on offer, ms, fastest first. Each is a whole multiple of
    /// every one before it, so a consumer on a rung is served exactly at
    /// whichever faster rung the part is run.
    static constexpr uint32_t skLadderMs[] = { 100, 200, 1000, 5000, 10000 };

    static constexpr size_t skRungs = sizeof(skLadderMs) / sizeof(skLadderMs[0]);

    static constexpr uint32_t skFastestMs = skLadderMs[0];
    static constexpr uint32_t skSlowestMs = skLadderMs[skRungs - 1];

    /**
     * @brief   Whether the ladder keeps its rule: rising, and each rung a
     *          whole multiple of the one before, which makes it one of every
     *          rung before.
     */
    static constexpr bool isLadderAChain()
    {
        for (size_t i = 1; i < skRungs; i++) {
            if ((skLadderMs[i] <= skLadderMs[i - 1])
                    || ((skLadderMs[i] % skLadderMs[i - 1]) != 0U)) {
                return false;
            }
        }

        return true;
    }

    static float maxRateHz() { return 1000.0f / static_cast<float>(skFastestMs); }
    static float minRateHz() { return 1000.0f / static_cast<float>(skSlowestMs); }

    /**
     * @brief   The rung a rate is served by: the slowest one at least as fast.
     * @param   rateHz: Rate asked for, Hz. Above the fastest rung it gets the
     *          fastest; below the slowest, or NaN, the slowest.
     * @retval  The rung's period, ms.
     */
    static uint32_t periodMsFor(float rateHz)
    {
        // Written as a failed lower-bound test so NaN is caught too.
        if (!(rateHz >= minRateHz())) {
            return skSlowestMs;
        }

        // Rounded to the microsecond: a rate a hair above a rung's own must
        // not fall through to the rung below it, at up to five times the
        // rate. A rate past the fast end comes out shorter than every rung
        // and keeps the fastest.
        const uint32_t askedUs =
                static_cast<uint32_t>((1000000.0f / rateHz) + 0.5f);

        uint32_t periodMs = skFastestMs;

        for (uint32_t rungMs : skLadderMs) {
            if ((rungMs * 1000U) <= askedUs) {
                periodMs = rungMs;
            }
        }

        return periodMs;
    }

    /**
     * @brief   Start a stream: the first measurement is due at once.
     */
    void start(uint32_t nowMs, uint32_t periodMs)
    {
        mPeriodMs = periodMs;
        mNextMs   = nowMs;
        mRunning  = true;
    }

    /**
     * @brief   Change the period of a running stream. The next measurement
     *          moves to one new period after the last one.
     */
    void retime(uint32_t periodMs)
    {
        if (!mRunning) {
            return;
        }

        mNextMs   = (mNextMs - mPeriodMs) + periodMs;
        mPeriodMs = periodMs;
    }

    void stop()
    {
        mRunning = false;
    }

    bool isRunning() const
    {
        return mRunning;
    }

    uint32_t periodMs() const
    {
        return mPeriodMs;
    }

    /**
     * @brief   Whether a measurement is due now; moves on to the next deadline
     *          if so.
     * @note    Each deadline is the previous one plus the period, not the
     *          moment the last one was noticed, so a late wake-up does not
     *          push every later measurement back with it. One a whole period
     *          late starts the grid over from the present rather than
     *          catching up with a burst.
     */
    bool take(uint32_t nowMs)
    {
        if (!mRunning) {
            return false;
        }

        const int32_t late = static_cast<int32_t>(nowMs - mNextMs);
        if (late < 0) {
            return false;
        }

        mNextMs = (static_cast<uint32_t>(late) >= mPeriodMs) ? (nowMs + mPeriodMs)
                                                             : (mNextMs + mPeriodMs);

        return true;
    }

    /**
     * @brief   Time to the next deadline, ms: 0 when it is due.
     * @note    Meaningful only while running.
     */
    uint32_t dueInMs(uint32_t nowMs) const
    {
        const int32_t left = static_cast<int32_t>(mNextMs - nowMs);

        return (left > 0) ? static_cast<uint32_t>(left) : 0U;
    }

private:

    bool     mRunning  = false;
    uint32_t mPeriodMs = skSlowestMs;
    uint32_t mNextMs   = 0;

};

static_assert(BaroCadence::isLadderAChain(),
        "a period on the ladder is not a whole multiple of every shorter one");

#endif // COMPONENT_BARO_CADENCE_HPP
