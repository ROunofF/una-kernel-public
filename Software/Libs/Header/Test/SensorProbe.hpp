/**
 ******************************************************************************
 * @file    SensorProbe.hpp
 * @brief   A listener that measures what it is given, and the checks
 *          that judge it; shared by the on-device SensorLayer tests
 *
 ******************************************************************************
 */

#ifndef TEST_SENSOR_PROBE_HPP
#define TEST_SENSOR_PROBE_HPP

#include "System.hpp"
#include "OS.hpp"

#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include <cstdint>

/**
 * @brief A listener that measures what it is given instead of using it.
 *
 * One connection to one sensor. Every delivery is timed against the clock
 * and its samples against each other. Two things are measured apart, as the
 * two kinds of fault are apart. Cadence - how often a delivery comes, how
 * many samples it carries and how old they are by then - starts over with
 * every scenario. Continuity - whether the timestamps run on from sample to
 * sample, through deliveries and through reconnections - is kept until it
 * is dropped on purpose.
 *
 * Filled from the thread that delivers the data and read from the test
 * thread.
 */
class Probe : public SDK::Interface::ISensorDataListener
{
public:
    struct Snapshot {
        uint32_t deliveries   = 0;
        uint32_t samples      = 0;
        uint32_t laterSamples = 0;      ///< All but the first delivery's: those the time first..last produced
        uint32_t minSamples   = 0;      ///< In one delivery
        uint32_t maxSamples   = 0;
        uint32_t firstMs      = 0;      ///< First and last delivery counted
        uint32_t lastMs       = 0;
        uint32_t gaps         = 0;
        uint32_t sumGapMs     = 0;
        uint32_t minGapMs     = 0;
        uint32_t maxGapMs     = 0;
        uint32_t lateGaps     = 0;      ///< Gaps over 5/4 of the expected one
        uint32_t sumOldestMs  = 0;      ///< Age of the oldest sample of a delivery
        int32_t  maxOldestMs  = 0;
        int32_t  minNewestMs  = 0;      ///< Age of the newest one
        int32_t  maxNewestMs  = 0;
        uint32_t stale        = 0;      ///< Deliveries whose oldest sample was over the limit
        uint32_t steps        = 0;
        uint32_t minStepUs    = 0;      ///< From one sample to the next, deliveries joined
        uint32_t maxStepUs    = 0;
        uint32_t zeroStamps   = 0;
        uint32_t duplicates   = 0;
        uint32_t backwards    = 0;
    };

    /// One delivery as the trace keeps it.
    struct Delivery {
        uint32_t atMs;          ///< When it came
        uint32_t firstMs;       ///< Stamps of its oldest and newest sample
        uint32_t lastMs;
        uint16_t count;
    };

    static constexpr uint32_t kTraceLength = 12;
    static constexpr uint32_t kTraceAfter  = 5;     ///< Deliveries kept after the odd one

    Probe(const char* name, SDK::Sensor::Type type)
        : mName(name)
        , mConnection(type, this)
    {
        mMutex.init(name);
    }

    const char* name() const
    {
        return mName;
    }

    bool connect(float periodMs, uint32_t latencyMs)
    {
        return mConnection.connect(periodMs, latencyMs);
    }

    void disconnect()
    {
        mConnection.disconnect();
    }

    /// Whether a delivery right behind another counts as odd. Not for a
    /// listener whose queue is smaller than a batch of its source: it is
    /// handed several deliveries a batch by design.
    void traceShortGaps(bool on)
    {
        OS::MutexCS cs(mMutex);
        mTraceShortGaps = on;
    }

    /**
     * @brief Start a new measurement and keep the continuity.
     * @param expectedGapMs Gap a late delivery is judged against, 0 not to judge.
     * @param staleMs       Age of the oldest sample a delivery is counted stale over.
     */
    void restart(uint32_t expectedGapMs, uint32_t staleMs)
    {
        OS::MutexCS cs(mMutex);

        const Snapshot old = mData;

        mData            = Snapshot {};
        mData.zeroStamps = old.zeroStamps;
        mData.duplicates = old.duplicates;
        mData.backwards  = old.backwards;
        mExpectedGapMs   = expectedGapMs;
        mStaleMs         = staleMs;
        mHavePrevMs      = false;
        mTraceCount      = 0;
        mTraceLeft       = 0;
        mTraceHit        = false;
    }

    /// Start a new measurement and a new run of timestamps.
    void restartAll(uint32_t expectedGapMs, uint32_t staleMs)
    {
        OS::MutexCS cs(mMutex);

        mData          = Snapshot {};
        mExpectedGapMs = expectedGapMs;
        mStaleMs       = staleMs;
        mHavePrevMs    = false;
        mHavePrevTs    = false;
        mTraceCount    = 0;
        mTraceLeft     = 0;
        mTraceHit      = false;
    }

    Snapshot get()
    {
        OS::MutexCS cs(mMutex);
        return mData;
    }

    /**
     * @brief The deliveries around the first odd gap of the measurement.
     * @param out Room for kTraceLength deliveries, oldest first.
     * @return How many were written; 0 when no gap was odd.
     */
    uint32_t trace(Delivery* out)
    {
        OS::MutexCS cs(mMutex);

        if (!mTraceHit) {
            return 0;
        }

        const uint32_t held = (mTraceCount < kTraceLength) ? mTraceCount : kTraceLength;
        for (uint32_t i = 0; i < held; ++i) {
            out[i] = mTrace[(mTraceCount - held + i) % kTraceLength];
        }

        return held;
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        if (count == 0 || !mConnection.matchesDriver(handle)) {
            return;
        }

        const uint32_t now = Driver::System::GetTimeMs();

        SDK::Sensor::DataBatch batch(base, count, stride);

        OS::MutexCS cs(mMutex);

        for (uint16_t i = 0; i < count; ++i) {
            const uint64_t ts = batch[i].getTimestampUs();

            if (ts == 0) {
                ++mData.zeroStamps;
                continue;
            }

            if (mHavePrevTs) {
                if (ts == mPrevTs) {
                    ++mData.duplicates;
                } else if (ts < mPrevTs) {
                    ++mData.backwards;
                } else {
                    track(static_cast<uint32_t>(ts - mPrevTs), mData.steps == 0, mData.minStepUs, mData.maxStepUs);
                    ++mData.steps;
                }
            }

            mPrevTs     = ts;
            mHavePrevTs = true;
        }

        const bool    first  = (mData.deliveries == 0);
        const int32_t oldest = static_cast<int32_t>(now - batch[0].getTimestamp());
        const int32_t newest = static_cast<int32_t>(now - batch[count - 1].getTimestamp());

        if (first || oldest > mData.maxOldestMs) {
            mData.maxOldestMs = oldest;
        }
        if (first || newest < mData.minNewestMs) {
            mData.minNewestMs = newest;
        }
        if (first || newest > mData.maxNewestMs) {
            mData.maxNewestMs = newest;
        }
        if (oldest > 0) {
            mData.sumOldestMs += static_cast<uint32_t>(oldest);
        }
        if (mStaleMs != 0 && oldest > static_cast<int32_t>(mStaleMs)) {
            ++mData.stale;
        }

        track(count, first, mData.minSamples, mData.maxSamples);

        bool odd = false;

        if (mHavePrevMs) {
            const uint32_t gap = now - mPrevMs;

            track(gap, mData.gaps == 0, mData.minGapMs, mData.maxGapMs);
            mData.sumGapMs += gap;
            ++mData.gaps;

            if (mExpectedGapMs != 0 && gap * 4 > mExpectedGapMs * 5) {
                ++mData.lateGaps;
                odd = true;
            }

            // Two deliveries on each other's heels are as odd as a late one.
            if (mExpectedGapMs != 0 && gap * 4 < mExpectedGapMs && mTraceShortGaps) {
                odd = true;
            }
        }

        if (mStaleMs != 0 && oldest > static_cast<int32_t>(mStaleMs)) {
            odd = true;
        }

        // The trace runs on until a few deliveries after the first odd gap
        // and then stands still, so it shows what led up to it and what
        // followed.
        if (!mTraceHit || mTraceLeft != 0) {
            mTrace[mTraceCount % kTraceLength] = { now, batch[0].getTimestamp(), batch[count - 1].getTimestamp(),
                                                   count };
            ++mTraceCount;

            if (mTraceHit) {
                --mTraceLeft;
            } else if (odd) {
                mTraceHit  = true;
                mTraceLeft = kTraceAfter;
            }
        }

        if (first) {
            mData.firstMs = now;
        }

        mPrevMs       = now;
        mHavePrevMs   = true;
        mData.lastMs  = now;
        mData.samples += count;
        if (!first) {
            mData.laterSamples += count;
        }
        ++mData.deliveries;
    }

private:
    static void track(uint32_t value, bool first, uint32_t& lo, uint32_t& hi)
    {
        if (first || value < lo) {
            lo = value;
        }
        if (first || value > hi) {
            hi = value;
        }
    }

    const char*        mName;
    Sensor::Connection mConnection;
    OS::Mutex          mMutex;
    Snapshot           mData;
    uint32_t           mExpectedGapMs = 0;
    uint32_t           mStaleMs       = 0;
    uint32_t           mPrevMs        = 0;
    bool               mHavePrevMs    = false;
    uint64_t           mPrevTs        = 0;
    bool               mHavePrevTs    = false;
    Delivery           mTrace[kTraceLength] {};
    uint32_t           mTraceCount    = 0;
    uint32_t           mTraceLeft     = 0;
    bool               mTraceHit      = false;
    bool               mTraceShortGaps = false;
};


/**
 * @brief The checks a scenario applies to its probes, and the count of those
 *        that failed.
 */
class SensorProbeTest
{
protected:
    /// What a scenario expects of a listener, as averages over its deliveries.
    struct Expect {
        uint32_t gapMinMs;
        uint32_t gapMaxMs;
        uint32_t samplesMin10;      ///< Samples in a delivery, times ten
        uint32_t samplesMax10;
        uint32_t oldestAvgMaxMs;    ///< Age of the oldest sample of a delivery
    };

    /**
     * @brief Let a change settle, then measure for a while.
     * @param newRun Whether the timestamps start a new run here, as they do
     *               after a first connection; false to hold them to the run
     *               that was going.
     */
    void measure(Probe& probe, const Expect& expect, uint32_t forMs, bool newRun);

    /**
     * @brief Print what a listener was given and judge it.
     *
     * The cadence and the age are judged by their averages: a single late
     * delivery is reported and not failed. The timestamps are judged sample
     * by sample: none zero, none twice, none out of order.
     */
    void check(const char* name, Probe& probe, const Expect& expect);

    /// How many samples a second a listener is given, and how far apart
    /// they are stamped, as a scenario expects them.
    struct Rate {
        uint32_t perSecMin10;       ///< Samples a second, times ten
        uint32_t perSecMax10;
        uint32_t stepMinUs;         ///< From one sample to the next
        uint32_t stepMaxUs;
    };

    /**
     * @brief Judge the rate a listener is given, from the last measurement.
     *
     * The step bounds are what makes this a check on the decimation: a
     * sample passed or dropped out of turn shows as a step of one source
     * step or of three, far outside them.
     */
    void checkRate(const char* name, Probe& probe, const Rate& expect);

    uint32_t mFailures = 0;
};

#endif // TEST_SENSOR_PROBE_HPP
