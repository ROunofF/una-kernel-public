/**
 ******************************************************************************
 * @file    SampleRateAdapter.hpp
 * @brief   Sample rate adapter
 *
 ******************************************************************************
 */

#ifndef __SENSOR_SAMPLE_RATE_ADAPTER_HPP
#define __SENSOR_SAMPLE_RATE_ADAPTER_HPP

#include <cstdint>

namespace Sensor
{
/**
 * @brief Per-listener delivery decimator.
 *
 * Passes every N-th sample of the source, N = max(1, floor(period / step)),
 * where `step` is the source's NOMINAL sampling step - the rate the part was
 * set to, not one measured from timestamps. The decision therefore does not
 * depend on how fast an individual chip's oscillator runs: a listener at 40 ms
 * on a 50 Hz source gets every second sample whether the stamps say 19.9, 20.0
 * or 20.1 ms, and the delivered stamps stay evenly spaced.
 *
 * A period that is not a whole multiple of the step is served no slower than
 * asked, up to twice as fast (N is rounded down). A listener that needs a rate
 * the source cannot produce resamples itself.
 *
 * A change of the nominal step - the source went to another rate for a
 * listener that joined or left - keeps the listener's place: until the next
 * delivery the count is the time since the last one, in whole steps of the
 * new length and to the nearest. The sample delivered next is then the one
 * nearest N new steps after the last, within half a step, if the source has
 * one there: a period after it, where the period is a whole number of steps.
 * A source that slows down gives its first sample at the new step when it
 * chooses, which can be most of two periods after the last one delivered;
 * none sooner than half a step short of N steps after it is delivered.
 *
 * The count restarts, delivering the sample that restarts it, on:
 * - the first sample after construction or reset();
 * - a step reported after none was;
 * - a timestamp that goes backwards;
 * - a gap: a sample arriving (N + 1/2) steps or more after the last one
 *   delivered, i.e. the N-th sample is overdue by half a step because frames
 *   were lost. The first sample after the gap is delivered rather than the one
 *   that would have fallen on the old grid: waiting for that one adds up to
 *   N - 1 steps of lateness, and the old phase means nothing to a listener.
 *
 * A source that reports no step (0) is passed through whole.
 *
 * @note All times here are MICROseconds. Sensor::DataQueue constructs this with
 *       the period in microseconds and feeds shouldEmit() the frame timestamp
 *       widened to microseconds. A microsecond uptime passes 2^32 after ~71.6
 *       minutes, so both the caller's conversion and the fields below must be
 *       64-bit.
 */
class SampleRateAdapter
{
public:
    /**
     * @brief Constructor
     * @param expectedPeriod Desired period between samples, in microseconds
     */
    explicit SampleRateAdapter(uint64_t expectedPeriod = 1000);

    /**
     * @brief Set the expected sampling period.
     * @param periodUs Period in microseconds
     */
    void setExpectedPeriod(uint64_t periodUs);

    /**
     * @brief Reset the internal state (e.g., after reconnect): the next sample
     *        is delivered and the count starts from it.
     */
    void reset();

    /**
     * @brief Check whether a sample carrying this timestamp should be forwarded.
     * @param ts     Frame timestamp in microseconds.
     * @param stepUs Nominal sampling step of the source in microseconds, 0 if
     *               the source does not report one.
     * @return true if the sample is to be delivered.
     */
    bool shouldEmit(uint64_t ts, uint32_t stepUs);

    /**
     * @brief A listener's period, given in ms, as this class takes it.
     *
     * Rounded to the microsecond, not cut: the period is divided by the
     * source step, and one that float arithmetic leaves a hair under a whole
     * multiple of it would otherwise lose one from the divisor.
     */
    static uint64_t periodUs(float periodMs);

    /**
     * @brief Decimation factor for a period on a source of the given step.
     * @param periodUs Requested period in microseconds.
     * @param stepUs   Nominal source step in microseconds, non-zero.
     * @return max(1, floor(periodUs / stepUs)).
     */
    static uint64_t divisor(uint64_t periodUs, uint32_t stepUs);

    /**
     * @brief Nominal step of the stream this adapter passes on.
     *
     * For a derived sensor whose input is a decimated stream: it reports this
     * as its own step so its listeners decimate against what they receive.
     *
     * @param periodUs Requested period in microseconds.
     * @param stepUs   Nominal source step in microseconds.
     * @return divisor(periodUs, stepUs) * stepUs; 0 when stepUs is 0, or when
     *         the product does not fit in 32 bits (over ~71.6 minutes), so
     *         the derived sensor's listeners are passed everything rather
     *         than counted against a wrapped step.
     */
    static uint32_t outputStepUs(uint64_t periodUs, uint32_t stepUs);

private:
    uint64_t mExpectedPeriod;       ///< Desired period in us
    uint64_t mLastEmitedTimestamp;  ///< Timestamp of last emitted sample, us
    uint64_t mCount;                ///< Samples since the last emitted one
    uint32_t mStepUs;               ///< Nominal step the count runs on, us
    bool     mStarted;              ///< A sample was delivered since reset()
    bool     mByStamps;             ///< The step changed since the last delivery
};

} // namespace Sensor

#endif // __SENSOR_SAMPLE_RATE_ADAPTER_HPP
