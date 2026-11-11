/**
 ******************************************************************************
 * @file    BaroRecoveryPolicy.hpp
 * @brief   What to do about a barometer stream that has stopped measuring.
 *
 *          The decision and its bookkeeping only, never the acting on it: no
 *          clock, bus or thread. Every answer is a function of the arguments,
 *          so the whole escalation can be checked without a part or an RTOS.
 *
 *          The stream is judged by the outcome of each measurement, not by
 *          silence: whoever runs it commands every measurement and knows at
 *          once how it went.
 ******************************************************************************
 */

#ifndef COMPONENT_BARO_RECOVERY_POLICY_HPP
#define COMPONENT_BARO_RECOVERY_POLICY_HPP

#include <cstdint>

/**
 * @brief   What to do about a stream that has stopped measuring.
 */
class BaroRecoveryPolicy
{

public:

    enum class Action : uint8_t {
        /// No stream is wanted, or it is measuring.
        NOTHING = 0,

        /// Take the supply away, wait out waitMs(), and bring the part up
        /// again.
        POWER_CYCLE,

        /// Out of attempts. Stop wanting the stream.
        GIVE_UP,
    };

    /**
     * @brief   Everything the decision depends on.
     */
    struct State {
        /// Whether a stream is meant to be up at all.
        bool     wanted        = false;

        /// Whether the part came up the last time it was powered.
        bool     up            = false;

        /// Measurements in a row that failed.
        uint32_t failedReads   = 0;

        /// Measurements in a row that returned the one before them again.
        uint32_t repeatedReads = 0;

        /// Measurements in a row that succeeded and differed from the one
        /// before them.
        uint32_t freshReads    = 0;

        /// Power cycles spent since the part last showed it was measuring.
        uint32_t attempts      = 0;
    };

    /**
     * @brief   How long the part stays unpowered before an attempt, in
     *          milliseconds: none before the first, then doubling up to a
     *          ceiling.
     * @param   attempts: Power cycles already spent.
     */
    static uint32_t waitMs(uint32_t attempts)
    {
        if (attempts == 0U) {
            return 0U;
        }

        // Clamped: a shift as wide as the type is undefined.
        const uint32_t shift = ((attempts - 1U) < skMaxShift) ? (attempts - 1U)
                                                              : skMaxShift;

        const uint32_t wait = skFirstWaitMs << shift;

        return (wait < skMaxWaitMs) ? wait : skMaxWaitMs;
    }

    /**
     * @brief   Decide what a stream is owed.
     */
    static Action decide(const State& state)
    {
        if (!state.wanted) {
            return Action::NOTHING;
        }

        const bool faulty = !state.up
                         || (state.failedReads   >= skMaxFailedReads)
                         || (state.repeatedReads >= skMaxRepeatedReads);
        if (!faulty) {
            return Action::NOTHING;
        }

        if (state.attempts >= skMaxAttempts) {
            return Action::GIVE_UP;
        }

        return Action::POWER_CYCLE;
    }

    /**
     * @brief   Record a measurement that failed.
     * @note    The run of repeats is kept: a failure is not a sign of life.
     */
    static void onReadFailed(State& state)
    {
        state.failedReads++;
        state.freshReads = 0;
    }

    /**
     * @brief   Record a measurement that succeeded.
     * @param   repeated: Whether it returned the one before it again.
     * @note    Only a measurement that differs from the one before shows the
     *          part is measuring, and only skMinFreshReads of them in a row
     *          clear the attempts spent. So a part that answers with one
     *          frozen value after every power cycle runs out of attempts,
     *          and so does one that measures once after each and fails
     *          again.
     */
    static void onReadOk(State& state, bool repeated)
    {
        state.failedReads = 0;

        if (repeated) {
            state.repeatedReads++;
            state.freshReads = 0;
            return;
        }

        state.repeatedReads = 0;

        if (state.freshReads < skMinFreshReads) {
            state.freshReads++;
        }

        if (state.freshReads >= skMinFreshReads) {
            state.attempts = 0;
        }
    }

    /**
     * @brief   Record a power cycle.
     * @param   up: Whether the part came up after it.
     */
    static void onPowerCycled(State& state, bool up)
    {
        state.attempts++;
        state.up            = up;
        state.failedReads   = 0;
        state.repeatedReads = 0;
        state.freshReads    = 0;
    }

    /// Measurements in a row that must fail, or repeat, before the part is
    /// power cycled.
    static constexpr uint32_t skMaxFailedReads   = 3;
    static constexpr uint32_t skMaxRepeatedReads = 5;

    /// Fresh measurements in a row that show the part is measuring again.
    static constexpr uint32_t skMinFreshReads = 5;

    /// Power cycles to spend on one fault before giving up.
    static constexpr uint32_t skMaxAttempts = 10;

    /// The wait before the second attempt, and the ceiling it doubles up to.
    static constexpr uint32_t skFirstWaitMs = 2000;
    static constexpr uint32_t skMaxWaitMs   = 30000;

private:

    /// Widest shift waitMs() will perform. Past the ceiling, and far short of
    /// the width of the type.
    static constexpr uint32_t skMaxShift = 8;

};

#endif // COMPONENT_BARO_RECOVERY_POLICY_HPP
