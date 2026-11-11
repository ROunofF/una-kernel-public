/**
 ******************************************************************************
 * @file    MagTimeBase.hpp
 * @brief   The instants a magnetometer stream's samples are published at.
 *
 *          Arithmetic only: no clock, bus or thread, so every answer is a
 *          function of the arguments. Intervals come from the part's own
 *          counter; the system clock, read when the interrupt was handled,
 *          anchors the stream. How fast the counter runs against that clock
 *          is estimated from the edges, not learnt through the error, and the
 *          correction the estimate leaves is slewed, never stepped. No instant
 *          is later than the read of its sample.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_TIME_BASE_HPP
#define COMPONENT_MAG_TIME_BASE_HPP

#include <cstdint>

class MagTimeBase
{

public:

    /// Build the instant to publish for this sample, and keep the two time
    /// bases together.
    /// @param sensorTime: The part's own count for this measurement.
    /// @param edgeMs:     When the interrupt that announced it was handled, or
    ///                    0 for a sample drained behind another. Used only to
    ///                    estimate the rate and steer the correction.
    /// @param readMs:     When the sample was read. Nothing published is later
    ///                    than readMs * 1000.
    /// @param periodUs:   The period the part is converting at.
    /// @return The instant, or skWithheld for a sample that has none: one the
    ///         read's bound would not let follow the last instant published.
    uint64_t stamp(uint32_t sensorTime, uint32_t edgeMs, uint32_t readMs,
                   uint32_t periodUs)
    {
        if (!mHave) {
            return anchor(sensorTime, edgeMs, readMs);
        }

        const uint32_t step = advance(sensorTime);

        // The system clock jumped: re-anchor so the stream's instants jump
        // with it.
        if ((edgeMs != 0) && clockJumped(edgeMs)) {
            reset();

            return anchor(sensorTime, edgeMs, readMs);
        }

        mSkewUs += steer(step, edgeMs, periodUs);

        return publish(readMs);
    }

    /// Forget the tie between the two bases. Nothing ties the counter to the
    /// system clock across a stop, so a new stream needs a new anchor and a
    /// new fit. The rate is the part's oscillator against the system clock,
    /// which a stop does not change: the next stream's fit starts from it.
    void reset()
    {
        mHave            = false;
        mCounts          = 0;
        mSkewUs          = 0;
        mLastPublishedUs = 0;
    }

    /// Slew ceiling, as a fraction of one step per sample, and the share of
    /// the error taken per sample.
    ///
    /// The ceiling must exceed the part's RC oscillator tolerance (+/-2% per
    /// the datasheet), or the correction pegs and the bases drift apart. The
    /// share is small because the error is measured against a millisecond
    /// tick, and a larger share steers by the tick noise.
    static constexpr uint32_t skSlewCeilingDivisor = 32;
    static constexpr uint32_t skSlewShare          = 64;

    /// How many edges the rate is estimated over: each sample's weight in the
    /// fit decays by 1/skFitSamples per edge after it. Long enough that the
    /// millisecond tick averages out of the rate, short enough to follow the
    /// oscillator as it warms.
    static constexpr uint32_t skFitSamples = 128;

    /// The fit's prior: the rate the stream starts from, held with the part's
    /// tolerance against edges this noisy. It keeps the first few edges,
    /// a millisecond apart on a millisecond clock, from reading as a rate of
    /// several per cent; a second of edges outweighs it.
    static constexpr uint32_t skEdgeNoiseUs  = 300;
    static constexpr uint32_t skRatePriorPpm = 20000;

    /// Where the correction steers, below the edge's millisecond. The edge
    /// happened within the millisecond it was stamped with and the read later
    /// still, so an instant a whole millisecond under it stays under the read
    /// whatever the tick noise, and the read's bound is left for a stream
    /// still settling.
    static constexpr uint32_t skTargetLeadUs = 1000;

    /// Returned for a sample that is not to be delivered: no instant both
    /// later than the last one and no later than its read exists.
    static constexpr uint64_t skWithheld = UINT64_MAX;

private:

    static constexpr int64_t kPpb          = 1000000000LL;
    static constexpr int64_t kRateLimitPpb = kPpb / skSlewCeilingDivisor;

    /// Tie the two bases on the first sample of a stream; everything after it
    /// is counted from here. The anchor is the fit's first edge.
    uint64_t anchor(uint32_t sensorTime, uint32_t edgeMs, uint32_t readMs)
    {
        const uint64_t latestUs = static_cast<uint64_t>(readMs) * 1000U;

        mAnchorUs  = target(edgeMs);
        mCounts    = 0;
        mLastCount = sensorTime;
        mSkewUs    = 0;
        mHave      = true;

        mLastPublishedUs = (mAnchorUs < latestUs) ? mAnchorUs : latestUs;

        mFitPrior       = static_cast<double>(mRatePpb) / static_cast<double>(kPpb);
        mFitPriorWeight = (mRateWeight > priorWeight()) ? mRateWeight : priorWeight();
        mFitWeight      = 1.0;
        mFitMeanX  = 0.0;
        mFitMeanZ  = 0.0;
        mFitVarX   = 0.0;
        mFitCovXZ  = 0.0;

        return mLastPublishedUs;
    }

    /// Take in the part's count, and return the counts since the last sample.
    uint32_t advance(uint32_t sensorTime)
    {
        // Masked to the counter's width so a wrap costs nothing.
        const uint32_t step = (sensorTime - mLastCount) & 0x00FFFFFFU;

        mCounts   += step;
        mLastCount = sensorTime;

        return step;
    }

    /// Wider than drift, slew offset and a stall can open, far narrower than
    /// a jump in the system clock such as its wrap.
    bool clockJumped(uint32_t edgeMs) const
    {
        constexpr int64_t kClockStepUs = 60LL * 1000LL * 1000LL;

        const int64_t error = static_cast<int64_t>(target(edgeMs))
                            - countedUs() - mSkewUs;

        return (error > kClockStepUs) || (error < -kClockStepUs);
    }

    /// How far to move the correction over this step: what the rate says the
    /// counter gained or lost, and, for a sample with an edge of its own, a
    /// share of the error that leaves. No more than the ceiling either way.
    int64_t steer(uint32_t step, uint32_t edgeMs, uint32_t periodUs)
    {
        const int64_t stepUs = static_cast<int64_t>(countsToUs(step));
        const int64_t limit  = (stepUs / skSlewCeilingDivisor) + 1;

        // Estimated only from, and steered only by, a sample with an edge of
        // its own: a drained one arrives when the bus finished, which says
        // nothing about the clocks. Nor by a read more than one and a half
        // periods after the last: the latched edge belongs to the first
        // pending conversion, the sample to the last.
        const uint32_t periodCounts = usToCounts(periodUs);
        const bool     edgeIsStale  = step > (periodCounts + (periodCounts / 2U));
        const bool     hasEdge      = (edgeMs != 0) && !edgeIsStale;

        if (hasEdge) {
            fit(edgeMs);
        }

        int64_t move = (stepUs * mRatePpb) / kPpb;

        if (hasEdge) {
            const int64_t error = static_cast<int64_t>(target(edgeMs))
                                - countedUs() - mSkewUs - move;

            int64_t share = error / static_cast<int64_t>(skSlewShare);

            // A small error's share rounds to nothing; one microsecond in the
            // right direction still closes it.
            if ((share == 0) && (error != 0)) {
                share = (error > 0) ? 1 : -1;
            }

            move += share;
        }

        if (move > limit) {
            move = limit;
        } else if (move < -limit) {
            move = -limit;
        }

        return move;
    }

    /// Take this edge into the rate: a least-squares slope of the edges
    /// against the counted time, over weights that decay by edge, and drawn
    /// towards the prior while the edges are few. Recursive, so the sums are
    /// kept as deviations from their means and stay small on a long stream.
    void fit(uint32_t edgeMs)
    {
        constexpr double kDecay = 1.0 - (1.0 / static_cast<double>(skFitSamples));

        // x: the counted time since the anchor; z: how far the edge sits from
        // it, whose slope against x is the rate.
        const double x = static_cast<double>(countsToUs(mCounts));
        const double z = static_cast<double>(static_cast<int64_t>(target(edgeMs))
                                             - static_cast<int64_t>(mAnchorUs))
                       - x;

        mFitWeight = (mFitWeight * kDecay) + 1.0;

        const double dx = x - mFitMeanX;
        mFitMeanX += dx / mFitWeight;
        mFitMeanZ += (z - mFitMeanZ) / mFitWeight;

        mFitVarX   = (mFitVarX * kDecay) + (dx * (x - mFitMeanX));
        mFitCovXZ  = (mFitCovXZ * kDecay) + (dx * (z - mFitMeanZ));

        // The prior fades with the edges, or it would hold the rate where an
        // oscillator that has since warmed or cooled once was.
        mFitPriorWeight *= kDecay;

        const double weight = mFitVarX + mFitPriorWeight;

        // Only a stream of edges with no counted time between them, once the
        // prior has faded, leaves nothing to divide by; the rate stands.
        if (!(weight > 0.0)) {
            return;
        }

        const double rate   = (mFitCovXZ + (mFitPriorWeight * mFitPrior)) / weight;

        // What the fit knows now, which the next stream's prior starts from.
        mRateWeight = weight;

        int64_t ratePpb = static_cast<int64_t>(rate * static_cast<double>(kPpb));

        if (ratePpb > kRateLimitPpb) {
            ratePpb = kRateLimitPpb;
        } else if (ratePpb < -kRateLimitPpb) {
            ratePpb = -kRateLimitPpb;
        }

        mRatePpb = ratePpb;
    }

    /// The instant for the sample just counted.
    uint64_t publish(uint32_t readMs)
    {
        const uint64_t latestUs = static_cast<uint64_t>(readMs) * 1000U;
        const int64_t  stamped  = countedUs() + mSkewUs;

        uint64_t published = (stamped > 0) ? static_cast<uint64_t>(stamped) : 0U;

        // Strictly increasing: a repeated or reversed instant reads as a stream
        // that restarted.
        if (published <= mLastPublishedUs) {
            published = mLastPublishedUs + 1U;
        }

        // Never later than the read. Where that leaves no room after the last
        // instant - a stream still converging with two reads in one
        // millisecond, or a clock that went back - the sample has no instant
        // and is withheld; the stream's count and correction move on.
        if (published > latestUs) {
            if (latestUs <= mLastPublishedUs) {
                return skWithheld;
            }

            published = latestUs;
        }

        mLastPublishedUs = published;

        return published;
    }

    /// The system time the part's counter reaches, uncorrected.
    int64_t countedUs() const
    {
        return static_cast<int64_t>(mAnchorUs + countsToUs(mCounts));
    }

    /// The weight of the datasheet's word on the rate, against edges this
    /// noisy: the fit's prior before it has learnt anything.
    static constexpr double priorWeight()
    {
        return (static_cast<double>(skEdgeNoiseUs) * static_cast<double>(skEdgeNoiseUs) * 1e12)
             / (static_cast<double>(skRatePriorPpm) * static_cast<double>(skRatePriorPpm));
    }

    /// The instant the correction steers this edge towards.
    static uint64_t target(uint32_t edgeMs)
    {
        const uint64_t edgeUs = static_cast<uint64_t>(edgeMs) * 1000U;

        return (edgeUs > skTargetLeadUs) ? (edgeUs - skTargetLeadUs) : 0U;
    }

    /// Sensortime counts as microseconds. One count is exactly 625/16 us.
    static uint64_t countsToUs(uint64_t counts)
    {
        return (counts * 625U) / 16U;
    }

    static uint32_t usToCounts(uint32_t us)
    {
        return static_cast<uint32_t>((static_cast<uint64_t>(us) * 16U) / 625U);
    }

    /// The anchor tying the part's counter to the system clock, and the counts
    /// run since, accumulated in 64 bits so the 24-bit counter wrap is
    /// seamless.
    uint64_t mAnchorUs  = 0;
    uint64_t mCounts    = 0;
    uint32_t mLastCount = 0;
    bool     mHave      = false;

    /// The correction towards the system clock, closed a little per sample,
    /// and the last instant published. A step would distort an interval, and
    /// a backwards instant reads as a new stream.
    int64_t  mSkewUs          = 0;
    uint64_t mLastPublishedUs = 0;

    /// How fast the part's counter runs against the system clock, in parts
    /// per billion, positive when the counter is slow, and how much the fit
    /// that gave it knew: both kept across streams as the next one's prior.
    int64_t mRatePpb    = 0;
    double  mRateWeight = 0.0;

    /// The stream's fit: the decayed weight of its edges, the means of the
    /// counted time and of the edges' distance from it, their decayed
    /// deviations, and the rate the fit started from with its weight.
    double mFitWeight      = 0.0;
    double mFitMeanX       = 0.0;
    double mFitMeanZ       = 0.0;
    double mFitVarX        = 0.0;
    double mFitCovXZ       = 0.0;
    double mFitPrior       = 0.0;
    double mFitPriorWeight = 0.0;

};

#endif // COMPONENT_MAG_TIME_BASE_HPP
