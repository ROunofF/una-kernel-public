/**
 ******************************************************************************
 * @file    RevolutionRateEstimator.cpp
 * @brief   Derives a revolution rate (rpm) from cumulative counters.
 ******************************************************************************
 *
 * Pure arithmetic only (no BlueNRG/OS/clock deps) so it is host-unit-tested.
 * See the header for the hardware-observed behaviours this has to survive.
 *
 ******************************************************************************
 */

#include "RevolutionRateEstimator.hpp"

namespace Ble
{

// Config is complete here, so brace-initialising it is fine at this point --
// which is exactly why the default-tunables overload delegates from the .cpp
// instead of using a `= Config {}` default argument in the header (see the note
// on the declaration).
RevolutionRateEstimator::RevolutionRateEstimator(CounterWidth width,
        RevTickRate rate)
    : RevolutionRateEstimator(width, rate, Config {})
{
}

RevolutionRateEstimator::RevolutionRateEstimator(CounterWidth width,
        RevTickRate rate, const Config &cfg)
    : mWidth(width)
    , mRate(rate)
    , mCfg(cfg)
{
}

void RevolutionRateEstimator::reset()
{
    mHavePrev     = false;
    mPrevRevs     = 0;
    mPrevTicks    = 0;
    mPrevSampleMs = 0;
    mLastSampleMs = 0;
    mLastChangeMs = 0;
    mRpm          = 0.0f;
    mValid        = false;
}

uint32_t RevolutionRateEstimator::ticksPerSecond() const
{
    return (mRate == RevTickRate::Hz2048) ? 2048u : 1024u;
}

uint32_t RevolutionRateEstimator::wrapPeriodMs() const
{
    // 65536 ticks / ticksPerSecond, in ms: 64000 at 1/1024 s, 32000 at 1/2048 s.
    return (65536u * 1000u) / ticksPerSecond();
}

uint32_t RevolutionRateEstimator::revsDelta(uint32_t revs) const
{
    // Unsigned wraparound gives the modular delta for free; the 16-bit case
    // just needs truncating to its own modulus.
    if (mWidth == CounterWidth::Bits16) {
        return static_cast<uint16_t>(revs - mPrevRevs);
    }
    return revs - mPrevRevs;
}

void RevolutionRateEstimator::resync(uint32_t nowMs, uint32_t revs,
        uint16_t ticks)
{
    mHavePrev     = true;
    mPrevRevs     = revs;
    mPrevTicks    = ticks;
    mPrevSampleMs = nowMs;
}

void RevolutionRateEstimator::onMeasurement(uint32_t nowMs,
        uint32_t cumulativeRevs, uint16_t eventTicks)
{
    mLastSampleMs = nowMs;

    if (!mHavePrev) {
        // No interval yet, so no rate can exist. Start the stall clock here so
        // a sensor that is already stationary still resolves to a valid zero.
        resync(nowMs, cumulativeRevs, eventTicks);
        mLastChangeMs = nowMs;
        return;
    }

    const uint32_t dRevs = revsDelta(cumulativeRevs);

    if (dRevs == 0) {
        // Stalled. A conforming sensor repeats the identical payload, so both
        // deltas are zero. Keep the existing anchor: when rotation resumes we
        // want the true interval from the last real revolution, not from here.
        if ((nowMs - mLastChangeMs) >= mCfg.stallTimeoutMs) {
            mRpm   = 0.0f;
            mValid = true;
        }
        return;
    }

    // Counters advanced, so this is a real revolution event.
    if ((nowMs - mPrevSampleMs) >= wrapPeriodMs()) {
        // The event-time counter may have wrapped more than once, which would
        // read as a short interval and inflate the rate. Cannot disambiguate,
        // so adopt the new anchor and derive nothing this time.
        resync(nowMs, cumulativeRevs, eventTicks);
        mLastChangeMs = nowMs;
        return;
    }

    const uint16_t dTicks = static_cast<uint16_t>(eventTicks - mPrevTicks);
    if (dTicks == 0) {
        // Revolutions advanced with no elapsed event time: malformed. Take the
        // counters but derive nothing -- never divide by zero.
        resync(nowMs, cumulativeRevs, eventTicks);
        mLastChangeMs = nowMs;
        return;
    }

    const float seconds = static_cast<float>(dTicks) /
            static_cast<float>(ticksPerSecond());
    const float rpm = (static_cast<float>(dRevs) * 60.0f) / seconds;

    resync(nowMs, cumulativeRevs, eventTicks);
    mLastChangeMs = nowMs;

    if (rpm > mCfg.maxRpm) {
        // Implausible -- most likely a rollover we failed to spot. Keep the
        // previous estimate rather than publishing nonsense.
        return;
    }

    mRpm   = rpm;
    mValid = true;
}

void RevolutionRateEstimator::tickMs(uint32_t nowMs)
{
    if (!mHavePrev) {
        return;
    }

    // Nothing arriving at all: the sensor or the link is gone. Note this is
    // checked FIRST -- silence must read as "no data", not as a stall.
    if ((nowMs - mLastSampleMs) >= mCfg.dataTimeoutMs) {
        mRpm   = 0.0f;
        mValid = false;
        return;
    }

    // Notifications arriving but the counters frozen: a real, valid zero.
    if ((nowMs - mLastChangeMs) >= mCfg.stallTimeoutMs) {
        mRpm   = 0.0f;
        mValid = true;
    }
}

}  /* namespace Ble */
