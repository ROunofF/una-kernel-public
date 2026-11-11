/**
 ******************************************************************************
 * @file    GravityTracker.hpp
 * @brief   Which way is down, from an accelerometer on a moving wrist.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_GRAVITY_TRACKER_HPP
#define COMPONENT_GRAVITY_TRACKER_HPP

#include <cmath>
#include <cstdint>

/**
 * @brief   Gravity, and whether it can be trusted right now.
 *
 *          An accelerometer reads gravity plus whatever the wrist is doing.
 *          Anything that levels a reading with it - a tilt-compensated bearing
 *          above all - wants the first without the second, and wants to know
 *          when it cannot have it.
 *
 *          Three things are done about that.
 *
 *            A low-pass filter. Readings at rest wobble with noise and small
 *            movements; a first-order filter with a time constant of kTauS
 *            takes those out. Its step is worked out from the time between
 *            samples, so it behaves the same at any rate.
 *
 *            A magnitude gate. A reading whose magnitude is outside
 *            [kMinG, kMaxG] of standard gravity is not gravity, and is not fed
 *            to the filter at all. It starts a disturbance, which ends once
 *            kSettleUs have passed with no such reading; the first reading
 *            back inside the gate restarts the filter from itself, so what
 *            comes out afterwards is the attitude the watch is now in rather
 *            than a blend with the one it was in before. A disturbance that
 *            goes on for kMaxHoldUs, readings outside the gate never far enough
 *            apart to settle between them, is reported as no gravity at all.
 *
 *            An age limit. Gravity more than kMaxAgeUs away from the instant
 *            it is asked for describes how the watch was held then, not now.
 *            The same limit, between two samples, restarts the filter.
 *
 *          Readings in any direction are welcome; this does not assume the
 *          watch is face up. Timestamps are those the samples carry, in
 *          microseconds on one time base with whatever they are paired with.
 *
 *          Not thread-safe: push() and query() are meant for one thread.
 */
class GravityTracker
{
public:
    enum class State : uint8_t {
        NONE,    ///< No gravity to give: none yet, too old, or moving too long.
        STEADY,  ///< Gravity is given.
        MOVING,  ///< The wrist is moving; gravity is not given for now.
    };

    /// Standard gravity, m/s^2, the unit readings are taken in.
    static constexpr float    kStandardGravity = 9.80665f;

    /// The magnitude gate, as fractions of standard gravity. Both inclusive.
    static constexpr float    kMinG = 0.8f;
    static constexpr float    kMaxG = 1.2f;

    /// The filter's time constant, seconds. It is also how far gravity lags a
    /// turn of the watch, and a bearing levelled with gravity that lags is a
    /// bearing that leans: kept short enough that a wrist turned at a readable
    /// speed leans by little, and long enough to take out the wobble.
    static constexpr float    kTauS = 0.1f;

    /// How long a disturbance lasts after its last reading outside the gate.
    static constexpr uint64_t kSettleUs = 300000U;

    /// How long a disturbance may last before it is reported as no gravity.
    static constexpr uint64_t kMaxHoldUs = 1000000U;

    /// Furthest gravity may be from the instant it is asked for, and furthest
    /// apart two samples may be for the filter to carry on across them.
    static constexpr uint64_t kMaxAgeUs = 500000U;

    /**
     * @brief   Forget everything, as if no sample had arrived.
     */
    void reset()
    {
        mHave     = false;
        mMoving   = false;
        mReseed   = false;
        mLastUs   = 0U;
        mOutSince = 0U;
        mOutLast  = 0U;
        mGx       = 0.0f;
        mGy       = 0.0f;
        mGz       = 0.0f;
    }

    /**
     * @brief   One accelerometer reading.
     * @param   tUs: When it was measured, microseconds.
     * @param   ax, ay, az: The reading, m/s^2, gravity included.
     * @note    A reading that is not a number is dropped: it says nothing
     *          about gravity or about movement.
     */
    void push(uint64_t tUs, float ax, float ay, float az)
    {
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az)) {
            return;
        }

        const float magnitudeSq = (ax * ax) + (ay * ay) + (az * az);
        const bool  inGate = (magnitudeSq >= kMinSq) && (magnitudeSq <= kMaxSq);

        // Nothing before, time run backwards, or too long since: whatever was
        // known is about another moment, so start again from this reading.
        const bool restart = !mHave || (tUs < mLastUs)
                || ((tUs - mLastUs) > kMaxAgeUs);

        const uint64_t lastUs = mLastUs;

        if (restart) {
            mHave   = true;
            mMoving = false;
            mReseed = true;
        }

        mLastUs = tUs;

        if (!inGate) {
            if (!mMoving) {
                mMoving   = true;
                mOutSince = tUs;
            }

            mOutLast = tUs;
            mReseed  = true;

            return;
        }

        if (mReseed) {
            mReseed = false;
            mGx     = ax;
            mGy     = ay;
            mGz     = az;
        } else {
            const float dtS   = static_cast<float>(tUs - lastUs) * 1e-6f;
            const float alpha = dtS / (kTauS + dtS);

            mGx += alpha * (ax - mGx);
            mGy += alpha * (ay - mGy);
            mGz += alpha * (az - mGz);
        }

        if (mMoving && ((tUs - mOutLast) >= kSettleUs)) {
            mMoving = false;
        }
    }

    /**
     * @brief   Gravity for an instant.
     * @param   tUs: The instant it is wanted for, microseconds.
     * @param   gx, gy, gz: Filtered gravity, m/s^2, pointing up out of the
     *          ground as the accelerometer reads it. Untouched unless STEADY.
     */
    State query(uint64_t tUs, float& gx, float& gy, float& gz) const
    {
        if (!mHave) {
            return State::NONE;
        }

        const uint64_t apart = (tUs >= mLastUs) ? (tUs - mLastUs)
                                                : (mLastUs - tUs);

        if (apart > kMaxAgeUs) {
            return State::NONE;
        }

        if (mMoving) {
            // Judged on the readings, not on the instant asked about: readings
            // can arrive late, and the wait for the next one is not movement.
            // Readings that stop coming are the age limit's business.
            const bool tooLong = (mLastUs - mOutSince) >= kMaxHoldUs;

            return tooLong ? State::NONE : State::MOVING;
        }

        gx = mGx;
        gy = mGy;
        gz = mGz;

        return State::STEADY;
    }

private:
    static constexpr float kMinSq =
            (kMinG * kStandardGravity) * (kMinG * kStandardGravity);
    static constexpr float kMaxSq =
            (kMaxG * kStandardGravity) * (kMaxG * kStandardGravity);

    bool     mHave     = false;  ///< A sample has arrived since the last reset.
    bool     mMoving   = false;  ///< A disturbance is under way.
    bool     mReseed   = false;  ///< The next reading in the gate restarts the filter.
    uint64_t mLastUs   = 0U;     ///< The latest sample's instant.
    uint64_t mOutSince = 0U;     ///< The disturbance's first reading outside the gate; never after mLastUs.
    uint64_t mOutLast  = 0U;     ///< Its latest one.
    float    mGx       = 0.0f;
    float    mGy       = 0.0f;
    float    mGz       = 0.0f;
};

#endif // COMPONENT_GRAVITY_TRACKER_HPP
