/**
 ******************************************************************************
 * @file    SourceTick.hpp
 * @brief   Sampling clock for a sensor that is read on the sweep
 *
 ******************************************************************************
 */

#ifndef SENSOR_SOURCE_TICK_HPP
#define SENSOR_SOURCE_TICK_HPP

#include <atomic>
#include <cstdint>

namespace Sensor
{

/**
 * @brief The sampling clock of a sensor that takes its sample on the sweep.
 *
 * A sensor with no clock of its own - it holds a value, or reads one when
 * asked - samples when this falls due, and reports the interval as its source
 * step. For that step to hold:
 *
 * - Each deadline is the previous one plus the interval, not the moment the
 *   last one was noticed, so a late sweep does not push every later sample
 *   back with it. A sweep more than a whole interval late starts the grid over
 *   from the present rather than catching up with a burst.
 * - The sweep wakes for the deadline: its driver passes dueInMs() on to the
 *   manager, which sleeps no longer than that.
 *
 * A deadline counts as reached this far ahead of it, since the sweep can wake
 * a little early for it (see Driver::sSweepLeadMs).
 *
 * start() and stop() may come from any thread; check() and dueInMs() belong to
 * the sweep. Times are the millisecond clock, compared modulo 2^32. That
 * clock wraps at 4,194,304,000 ms, about every 48.5 days, not at 2^32, so
 * across its wrap a deadline reads as long overdue: one sample comes up to an
 * interval early, and the grid starts over from it.
 */
class SourceTick
{
public:
    static constexpr uint32_t kNone = UINT32_MAX;   ///< dueInMs(): nothing due

    /// Run at this interval, from the next sweep; the first sample is one
    /// interval after that.
    void start(uint32_t intervalMs)
    {
        mRequested.store((intervalMs != 0) ? intervalMs : 1, std::memory_order_relaxed);
        mRestart.store(true, std::memory_order_release);
    }

    void stop()
    {
        mRequested.store(0, std::memory_order_relaxed);
    }

    bool isActive() const
    {
        return mRequested.load(std::memory_order_relaxed) != 0;
    }

    /// The interval asked for, ms; 0 when stopped.
    uint32_t intervalMs() const
    {
        return mRequested.load(std::memory_order_relaxed);
    }

    /// The interval as the source step a driver reports, us; 0 when stopped,
    /// or when it does not fit in 32 bits (over ~71.6 minutes), so listeners
    /// are passed everything rather than counted against a wrapped step.
    uint32_t stepUs() const
    {
        const uint64_t us = static_cast<uint64_t>(intervalMs()) * 1000U;

        return (us <= UINT32_MAX) ? static_cast<uint32_t>(us) : 0;
    }

    /**
     * @brief Whether a sample is due now; moves on to the next deadline if so.
     * @param nowMs  The millisecond clock.
     * @param leadMs How far ahead of a deadline it counts as reached.
     */
    bool check(uint32_t nowMs, uint32_t leadMs)
    {
        // The start is taken up before its interval is read: a start() that
        // lands in between leaves its flag set for the next call, rather
        // than having it cleared against the old interval.
        const bool     restart  = mRestart.exchange(false, std::memory_order_acquire);
        const uint32_t interval = mRequested.load(std::memory_order_relaxed);

        if (interval == 0) {
            mRunning = false;
            return false;
        }

        if (restart || !mRunning) {
            mRunning  = true;
            mInterval = interval;
            mNext     = nowMs + interval;
            return false;
        }

        const int64_t late = static_cast<int32_t>(nowMs - mNext);

        if (late + static_cast<int64_t>(leadMs) < 0) {
            return false;
        }

        mNext = (late >= static_cast<int64_t>(mInterval)) ? (nowMs + mInterval)
                                                           : (mNext + mInterval);

        return true;
    }

    /// Time to the next deadline, ms: 0 when it is due or a start is waiting
    /// to be taken up, kNone when stopped.
    uint32_t dueInMs(uint32_t nowMs) const
    {
        if (mRequested.load(std::memory_order_relaxed) == 0) {
            return kNone;
        }

        if (mRestart.load(std::memory_order_acquire) || !mRunning) {
            return 0;
        }

        const int32_t left = static_cast<int32_t>(mNext - nowMs);

        return (left > 0) ? static_cast<uint32_t>(left) : 0;
    }

private:
    std::atomic<uint32_t> mRequested{0};    ///< Asked for; 0 = stopped
    std::atomic<bool>     mRestart{false};  ///< A start not yet taken up

    // The sweep's own
    bool     mRunning   = false;
    uint32_t mInterval  = 0;               ///< The interval the grid runs on
    uint32_t mNext      = 0;                ///< Next deadline
};

} // namespace Sensor

#endif // SENSOR_SOURCE_TICK_HPP
