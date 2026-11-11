/**
 ******************************************************************************
 * @file    MagRecoveryPolicy.hpp
 * @brief   What to do about a magnetometer stream that may have stopped.
 *
 *          The decision only, never the acting on it: no clock, bus or
 *          thread. Every answer is a function of the arguments, so the whole
 *          escalation can be checked without a part or an RTOS.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_RECOVERY_POLICY_HPP
#define COMPONENT_MAG_RECOVERY_POLICY_HPP

#include <cstdint>

/**
 * @brief   What to do about a stream that may have stopped.
 */
class MagRecoveryPolicy
{

public:

    enum class Action : uint8_t {
        /// No stream is wanted, or the deadline has not passed yet.
        NOTHING = 0,

        /// Read the interrupt status. A stalled stream ends with the interrupt
        /// latched and unread; the read releases it, so the next conversion
        /// raises a fresh edge. Cheap, and tried first.
        RELEASE_LATCH,

        /// Take the stream down and build it again.
        RESTART,

        /// Out of attempts. Stop wanting the stream.
        GIVE_UP,
    };

    /**
     * @brief   Everything the decision depends on.
     */
    struct State {
        /// Whether a stream is meant to be up at all.
        bool     wanted            = false;

        /// Whether one is actually delivering; decides whether there is a
        /// configured stream to nudge.
        bool     running           = false;

        /// How long since a sample was last read - read, not delivered.
        uint32_t sinceLastSampleMs = 0;

        /// Recovery actions already spent on this stall, zero once a sample
        /// arrives.
        uint32_t attempts          = 0;

        /// The period the chip was last told to run at.
        uint32_t appliedPeriodUs   = 0;
    };

    /**
     * @brief   How long a stream may go without a sample before it counts as
     *          stalled, in milliseconds. Scales with the period.
     * @note    The floor keeps bus contention from reading as a stall on the
     *          fast rungs.
     */
    static uint32_t deadlineMs(uint32_t appliedPeriodUs)
    {
        const uint32_t byRate = (appliedPeriodUs / 1000U) * skPeriods;

        return (byRate > skFloorMs) ? byRate : skFloorMs;
    }

    /**
     * @brief   How long to wait before looking again, in milliseconds: the
     *          deadline, doubling per attempt up to a ceiling.
     */
    static uint32_t waitMs(uint32_t appliedPeriodUs, uint32_t attempts)
    {
        // Clamped: a shift as wide as the type is undefined.
        const uint32_t shift = (attempts < skMaxShift) ? attempts : skMaxShift;

        uint32_t factor = 1U << shift;
        if (factor > skMaxBackoff) {
            factor = skMaxBackoff;
        }

        return deadlineMs(appliedPeriodUs) * factor;
    }

    /**
     * @brief   Decide what a stream that may have stalled is owed.
     * @note    Deliberately blind to the cause: every stall shows up here as
     *          the same silence.
     */
    static Action decide(const State& state)
    {
        if (!state.wanted) {
            return Action::NOTHING;
        }

        if (state.sinceLastSampleMs <
                waitMs(state.appliedPeriodUs, state.attempts)) {
            return Action::NOTHING;
        }

        if (state.attempts >= skMaxAttempts) {
            return Action::GIVE_UP;
        }

        // The cheap fix is worth one try, and only with a configured stream:
        // with the chip down there is no latch to release.
        if ((state.attempts == 0U) && state.running) {
            return Action::RELEASE_LATCH;
        }

        return Action::RESTART;
    }

    /// Deadline for the next sample, in periods and in milliseconds; the
    /// larger wins.
    static constexpr uint32_t skPeriods = 4;
    static constexpr uint32_t skFloorMs = 1000;

    /// Actions to spend on one stall before giving up, and how far the wait
    /// between them may double.
    static constexpr uint32_t skMaxAttempts = 10;
    static constexpr uint32_t skMaxBackoff  = 8;

private:

    /// Widest shift waitMs() will perform. Past the backoff ceiling, and far
    /// short of the width of the type.
    static constexpr uint32_t skMaxShift = 8;

};

#endif // COMPONENT_MAG_RECOVERY_POLICY_HPP
