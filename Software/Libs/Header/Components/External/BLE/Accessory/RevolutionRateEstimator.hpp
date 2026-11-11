/**
 ******************************************************************************
 * @file    RevolutionRateEstimator.hpp
 * @brief   Derives a revolution rate (rpm) from cumulative counters.
 ******************************************************************************
 *
 * CPS (0x1818) and CSC (0x1816) both report rotation as a CUMULATIVE revolution
 * count plus the event timestamp of the most recent revolution, never as an
 * instantaneous rate. Rate = delta-revs / delta-time, and the same arithmetic
 * serves four cases -- CPS crank, CPS wheel, CSC crank, CSC wheel -- differing
 * only in counter width and tick rate. This is that arithmetic, once, so the
 * awkward parts are written and tested in a single place instead of four.
 *
 * The awkward parts, all observed on real hardware:
 *
 *  - ROLLOVER IS ROUTINE, NOT EXCEPTIONAL. The 16-bit event-time counter wraps
 *    every 64 s at 1/1024 s (32 s at 1/2048 s), so a ride crosses it constantly.
 *    The crank revolution counter is 16-bit and wraps every 65536 revolutions
 *    (~12 h of pedalling accumulated over the sensor's life, so it lands
 *    mid-activity sooner or later); the wheel counter is 32-bit and effectively
 *    never wraps. Each is handled at its own modulus.
 *  - A STALLED SENSOR KEEPS NOTIFYING UNCHANGED COUNTERS. A stationary Garmin
 *    CSC sensor notifies at its normal rate with byte-identical payloads, so
 *    delta-revs and delta-time are BOTH zero. Never divide by zero, never hold
 *    the last rate indefinitely, and resolve to a VALID zero rather than to
 *    "no data" -- a rider stopped pedalling, which is a fact, not a dropout.
 *  - EVENT TIME IS NOT HOST TIME. It timestamps the last revolution, which may
 *    predate our subscription, so the first derived interval can legitimately
 *    exceed the host-clock gap. Host time is therefore used only as a coarse
 *    guard against an ambiguous (multi-wrap) gap, never as the rate's basis.
 *
 * Pure logic: no BlueNRG, OS or clock dependency (the caller supplies the host
 * timestamp), so it is exercised directly by host unit tests.
 *
 ******************************************************************************
 */

#ifndef REVOLUTION_RATE_ESTIMATOR_HPP
#define REVOLUTION_RATE_ESTIMATOR_HPP

#include <cstdint>

#include "IAccessoryProfile.hpp"

namespace Ble
{

/**
 * @brief   Turns cumulative revolution counters into a rate in rpm.
 */
class RevolutionRateEstimator {
public:

    /**
     * @brief   Wire width of the cumulative revolution counter, which sets the
     *          modulus its rollover is computed at.
     *
     * Crank revolutions are uint16 in both CPS and CSC; wheel revolutions are
     * uint32 in both. Using the wrong one silently mangles any delta that
     * crosses 65536.
     */
    enum class CounterWidth : uint8_t {
        Bits16,   ///< crank revolution counters
        Bits32,   ///< wheel revolution counters
    };

    /**
     * @brief   Tunables, exposed so host tests can drive the timeouts directly.
     */
    struct Config {
        /// Counters unchanged for this long => confirmed stall: 0 rpm, VALID.
        /// 4 s admits cadences down to 15 rpm without flapping at the low end.
        uint32_t stallTimeoutMs { 4000 };
        /// No notification at all for this long => no data: INVALID. Distinct
        /// from a stall, which arrives as a steady stream of unchanged values.
        uint32_t dataTimeoutMs { 5000 };
        /// Reject a derived rate above this as garbage. Deliberately loose: a
        /// wheel at 40 km/h on a 2.1 m tyre already exceeds 300 rpm, so this
        /// only catches nonsense (e.g. a missed rollover), not fast riding.
        float maxRpm { 2000.0f };
    };

    /**
     * @brief   The current estimate.
     *
     * `valid == true` with `rpm == 0` means a confirmed stall (stopped
     * pedalling). `valid == false` means no usable data -- no sensor, no link,
     * or not enough samples yet. Conflating the two is what shows a rider a
     * frozen cadence, so they are deliberately distinct.
     */
    struct Sample {
        float rpm   { 0.0f };
        bool  valid { false };
    };

    /**
     * @brief   Construct for one counter with the default tunables.
     * @param   width: wire width of the revolution counter.
     * @param   rate: tick rate of the event-time counter.
     *
     * @note    Deliberately two constructors rather than one with a
     *          `= Config {}` default argument: GCC rejects that, because a
     *          default member initializer of Config cannot be used inside the
     *          enclosing class's own definition ("required before the end of
     *          its enclosing class"). MSVC accepts it, so the host build alone
     *          will not catch a regression here -- do not "simplify" this back.
     */
    RevolutionRateEstimator(CounterWidth width, RevTickRate rate);

    /**
     * @brief   Construct for one counter with explicit tunables.
     * @param   width: wire width of the revolution counter.
     * @param   rate: tick rate of the event-time counter.
     * @param   cfg: timeouts and sanity bound.
     */
    RevolutionRateEstimator(CounterWidth width, RevTickRate rate,
            const Config &cfg);

    /**
     * @brief   Forget all history (on disconnect / re-acquire). The next
     *          measurement is treated as a first sample.
     */
    void reset();

    /**
     * @brief   Feed one decoded notification.
     * @param   nowMs: host monotonic time the notification arrived.
     * @param   cumulativeRevs: counter as decoded, widened to 32 bits.
     * @param   eventTicks: event-time counter as decoded (always 16-bit).
     */
    void onMeasurement(uint32_t nowMs, uint32_t cumulativeRevs,
            uint16_t eventTicks);

    /**
     * @brief   Periodic re-evaluation, required because neither a stall nor a
     *          dropout is signalled by an incoming sample. Safe to call at any
     *          cadence; it only ever expires timeouts.
     * @param   nowMs: host monotonic time.
     */
    void tickMs(uint32_t nowMs);

    /**
     * @brief   The current estimate.
     */
    Sample getSample() const { return Sample { mRpm, mValid }; }

private:

    /// Event-time ticks per second for the configured rate.
    uint32_t ticksPerSecond() const;

    /// Host-time span after which a 16-bit event-time delta is ambiguous
    /// because the counter may have wrapped more than once.
    uint32_t wrapPeriodMs() const;

    /// Revolution delta at the configured counter's modulus.
    uint32_t revsDelta(uint32_t revs) const;

    /// Adopt these counters as the new anchor without deriving a rate.
    void resync(uint32_t nowMs, uint32_t revs, uint16_t ticks);

    CounterWidth mWidth;
    RevTickRate  mRate;
    Config       mCfg;

    bool     mHavePrev     { false };
    uint32_t mPrevRevs     { 0 };
    uint16_t mPrevTicks    { 0 };
    uint32_t mPrevSampleMs { 0 };  ///< host time of the anchor sample
    uint32_t mLastSampleMs { 0 };  ///< host time of the last notification, any
    uint32_t mLastChangeMs { 0 };  ///< host time the counters last advanced

    float mRpm   { 0.0f };
    bool  mValid { false };
};

}  /* namespace Ble */

#endif /* REVOLUTION_RATE_ESTIMATOR_HPP */
