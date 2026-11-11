/**
 ******************************************************************************
 * @file    LogThrottle.hpp
 * @brief   Rate limit for an error that can repeat at a data rate
 ******************************************************************************
 */

#ifndef LOG_THROTTLE_HPP
#define LOG_THROTTLE_HPP

#include <atomic>
#include <cstdint>

namespace App {

/**
 * @brief   Decides which failures of a repeating error to log.
 *
 * @details
 * Some failures repeat once per sensor sample or per message for as long as
 * their cause lasts - a full queue, an empty message pool. Logged every time,
 * one app that stops draining its queue writes two lines per sample, saturates
 * the log UART and takes the watch to a watchdog reset. The storm also fills
 * the crash ring within a second, so the capsule it leaves holds nothing from
 * before it started.
 *
 * A burst starts with a failure and ends with the first success that comes a
 * full window after the last failure. Ending it on the first success of any
 * kind would not work: a queue that is drained slower than it is fed sits
 * full, so its pushes alternate between failing and succeeding, and every
 * failure would then look like the start of a new burst.
 *
 * The first failure of a burst is logged in full. After that at most one
 * summary line is logged per window, carrying the count that went unlogged
 * since the previous line. Failures that resume after a quiet window with no
 * success in between continue the same burst, so the first of them logs a
 * summary that includes them. When the burst ends, onSuccess() returns its
 * total if it was more than a single failure, so the caller can say that it
 * recovered. The quiet failures at the end of a burst are reported only if
 * the site is called again; if the app is unloaded first, they are not.
 *
 * The caller owns the wording. It acts on the result of each call with its
 * own LOG_* line, so the first line of a burst can stay byte-for-byte what the
 * site logged before this existed - field-crash tooling matches on those
 * strings.
 *
 * Every member is atomic and no call blocks, so an instance can be shared by
 * threads that fail at the same time. It is deliberately not exact under that
 * contention: a failure racing the success that ends a burst can log one line
 * more than needed, or land in the neighbouring burst's count. That is the
 * right trade for a log line - the alternative is a lock on a path that runs
 * per sample. Callers pass the time rather than this reading a clock, which
 * keeps the class free of RTOS dependencies and testable on a host.
 *
 * Elapsed time is an unsigned subtraction, which survives any wrap of the
 * clock - including OS::GetTimeMs(), which wraps at 4,194,304,000 ms rather
 * than 2^32. Across that wrap the window looks long, so the worst case is one
 * summary early. A time that appears to run backwards by less than a window
 * (a caller preempted between reading the clock and calling in) is ignored
 * rather than read as a gap of nearly 2^32 ms.
 */
class LogThrottle {
public:
    /// Long enough that a storm at a sensor rate costs a handful of lines a
    /// minute, and that a crash ring keeps minutes of context from before it.
    static constexpr uint32_t kDefaultWindowMs = 30000;

    /// What the caller should log for one failure.
    enum class Action : uint8_t {
        FIRST,      ///< First failure of a burst: log the site's full line.
        SUMMARY,    ///< The window has passed: log with Decision::suppressed.
        QUIET,      ///< Inside the window: log nothing.
    };

    struct Decision {
        Action   action;
        uint32_t suppressed;   ///< Failures not logged since the last line.
    };

    explicit constexpr LogThrottle(uint32_t windowMs = kDefaultWindowMs)
        : mWindowMs(windowMs)
    {}

    LogThrottle(const LogThrottle&)            = delete;
    LogThrottle& operator=(const LogThrottle&) = delete;

    /**
     * @brief   Record one failure.
     * @param   nowMs  Current time in ms, on any monotonic clock.
     * @return  What to log. suppressed is set only for SUMMARY.
     */
    Decision onFailure(uint32_t nowMs)
    {
        const uint32_t stamp = stampOf(nowMs);
        mLastFailMs.store(stamp, std::memory_order_relaxed);
        mBurstTotal.fetch_add(1, std::memory_order_relaxed);

        // mLastLogMs doubles as the burst flag, so there is no moment at
        // which a burst is running but its timestamp is not yet set - a second
        // failure racing the first either loses this exchange and reads the
        // winner's stamp, or finds the burst already open.
        uint32_t last = mLastLogMs.load(std::memory_order_relaxed);
        if (last == kNoBurst &&
                mLastLogMs.compare_exchange_strong(last, stamp,
                        std::memory_order_relaxed)) {
            return { Action::FIRST, 0 };
        }

        // Only the thread that moves the timestamp logs the summary, so two
        // threads crossing the window together do not both report it.
        if (hasElapsed(last, nowMs) &&
                mLastLogMs.compare_exchange_strong(last, stamp,
                        std::memory_order_relaxed)) {
            return { Action::SUMMARY,
                     mSuppressed.exchange(0, std::memory_order_relaxed) };
        }

        mSuppressed.fetch_add(1, std::memory_order_relaxed);
        return { Action::QUIET, 0 };
    }

    /**
     * @brief   Record one success, ending the burst if it has gone quiet.
     * @param   nowMs  Called for the time only when a burst is running, so the
     *                 path that succeeds thousands of times per failure costs
     *                 one relaxed load and no clock read.
     * @return  The number of failures in the burst this ended, when that was
     *          more than one; 0 otherwise - when no burst was running, when it
     *          has not yet been quiet for a window, or when it was a single
     *          failure, which was already logged in full.
     */
    template <typename NowMs>
    uint32_t onSuccess(NowMs nowMs)
    {
        uint32_t last = mLastLogMs.load(std::memory_order_relaxed);
        if (last == kNoBurst) {
            return 0;
        }
        if (!hasElapsed(mLastFailMs.load(std::memory_order_relaxed), nowMs())) {
            return 0;   // failed within the last window: the burst goes on
        }
        if (!mLastLogMs.compare_exchange_strong(last, kNoBurst,
                std::memory_order_relaxed)) {
            return 0;   // another thread ended or advanced it first
        }

        mSuppressed.store(0, std::memory_order_relaxed);
        const uint32_t total = mBurstTotal.exchange(0, std::memory_order_relaxed);
        return (total > 1) ? total : 0;
    }

private:
    /// mLastLogMs value meaning no burst is running. A real time of 0 is
    /// stored as 1 instead - 1 ms is well inside any window.
    static constexpr uint32_t kNoBurst = 0;

    static constexpr uint32_t stampOf(uint32_t nowMs)
    {
        return (nowMs == kNoBurst) ? 1 : nowMs;
    }

    /// At least one window since `since`, and not an apparent step backwards.
    bool hasElapsed(uint32_t since, uint32_t nowMs) const
    {
        const uint32_t elapsed = nowMs - since;
        return (elapsed >= mWindowMs) && (elapsed <= UINT32_MAX - mWindowMs);
    }

    const uint32_t        mWindowMs;
    std::atomic<uint32_t> mLastLogMs {kNoBurst};
    std::atomic<uint32_t> mLastFailMs {0};
    std::atomic<uint32_t> mSuppressed {0};
    std::atomic<uint32_t> mBurstTotal {0};
};

} // namespace App

#endif // LOG_THROTTLE_HPP
