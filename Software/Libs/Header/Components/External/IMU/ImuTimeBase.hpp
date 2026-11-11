/**
 ******************************************************************************
 * @file    ImuTimeBase.hpp
 * @brief   The system time of the IMU's frames, from the part's own counter.
 *
 *          Arithmetic only: no clock, bus or thread, so every answer is a
 *          function of the arguments. Every frame carries the value of the
 *          part's counter; once a read the newest frame's value is set against
 *          the system time the read was handed over at. From those references
 *          the base keeps a line from the counter to the system clock: how
 *          fast the counter runs is fitted to them, and the line is held just
 *          under the earliest of the last hand-overs, since a hand-over only
 *          ever comes after its frame. The line is moved a little a read,
 *          never stepped, save for a frame that would be later than its read.
 *
 *          The part's counter runs on one of two oscillators, by whether the
 *          gyroscope runs, each with a rate of its own. The base keeps a rate
 *          for each and has to be told of the switch.
 ******************************************************************************
 */

#ifndef COMPONENT_IMU_TIME_BASE_HPP
#define COMPONENT_IMU_TIME_BASE_HPP

#include <cstdint>

class ImuTimeBase
{

public:

    /// Take in a batch of frames, before its frames are given their times.
    /// A read may hand over several batches; of those the base keeps the one
    /// whose newest frame is the newest, so the order they come in and which
    /// stream each is of do not matter.
    /// @param sensorTime: The counter value of the batch's newest frame.
    /// @param nowUs:      The system time the read was handed over at: one
    ///                    value for all the batches of a read.
    void observe(uint32_t sensorTime, uint64_t nowUs)
    {
        sensorTime &= skCounterMask;

        if (!mHave) {
            anchor(sensorTime, nowUs);

            return;
        }

        const bool sameRead = (nowUs == mReadNowUs);

        // The system clock went back, or ran on for so long that the counter
        // cannot say how far it went itself.
        if (!sameRead && ((nowUs < mRefNowUs) || ((nowUs - mRefNowUs) > skLongestGapUs))) {
            anchor(sensorTime, nowUs);

            return;
        }

        const int32_t step = ahead(sensorTime, mRef.count);

        if (step <= 0) {
            // Every read empties the FIFO, so a new one brings no frame
            // from before those of the last. One far behind them is of a
            // counter that started anew.
            if (!sameRead && (step < -static_cast<int32_t>(skRestartCounts))) {
                anchor(sensorTime, nowUs);
            }

            return;
        }

        if (!sameRead && clockStepped(step, nowUs)) {
            anchor(sensorTime, nowUs);

            return;
        }

        if (!mFitting) {
            resume(sensorTime, nowUs);
        } else if (sameRead) {
            refine(sensorTime, nowUs);
        } else {
            mUndo         = mLine;
            mReadFrom     = mRef;
            mReadNowUs    = nowUs;
            mReadIsOrigin = false;

            advance(sensorTime, step, nowUs, false);
        }
    }

    /// The system time of a frame.
    /// @param sensorTime: The frame's counter value.
    /// @param nowUs:      The system time its read was handed over at. No
    ///                    frame is given a time later than this.
    /// @return The time, in microseconds of the system clock. The frames of
    ///         a read are spaced evenly from the read before to this one;
    ///         a frame past the last batch taken in runs on at the fitted
    ///         rate.
    uint64_t stamp(uint32_t sensorTime, uint64_t nowUs) const
    {
        if (!mHave) {
            return nowUs;
        }

        sensorTime &= skCounterMask;

        int64_t at = 0;

        const int32_t past = ahead(sensorTime, mRef.count);

        if (past > 0) {
            at = static_cast<int64_t>(mRef.us) + countsToUs(past, mRatePpb);
        } else {
            const int32_t into = ahead(sensorTime, mPrev.count);

            if (into > 0) {
                const int64_t span = ahead(mRef.count, mPrev.count);
                const int64_t took = static_cast<int64_t>(mRef.us - mPrev.us);

                at = static_cast<int64_t>(mPrev.us) + ((into * took) / span);
            } else {
                at = static_cast<int64_t>(mPrev.us) + countsToUs(into, mBeforePpb);
            }
        }

        if (at < 0) {
            at = 0;
        }

        const uint64_t stamped = static_cast<uint64_t>(at);

        return (stamped < nowUs) ? stamped : nowUs;
    }

    /// The gyroscope was switched on or off: from here the counter runs on
    /// the other oscillator. Frames taken before keep the rate they had.
    /// @param running: Whether the gyroscope runs now.
    /// @param nowUs:   The system time of the switch.
    void setGyroRunning(bool running, uint64_t nowUs)
    {
        const uint8_t clock = running ? 1U : 0U;

        if (clock == mClock) {
            return;
        }

        const int64_t ratePpb = mRatePpb;

        mClock   = clock;
        mRatePpb = toPpb(mRate[mClock].rate);
        mFitting = false;

        if (!mHave) {
            return;
        }

        if (nowUs <= mRef.us) {
            return;
        }

        if ((nowUs - mRef.us) > skLongestGapUs) {
            mHave = false;

            return;
        }

        // Where the counter stood at the switch, by the rate it had till
        // then: a reference of its own, so that frames either side of it
        // are laid at their own rate. It is set on a whole count, at the
        // time that count has on the line so far.
        const uint32_t counts = usToCounts(nowUs - mRef.us, ratePpb);

        if (counts != 0U) {
            const uint64_t at = mRef.us + static_cast<uint64_t>(countsToUs(counts, ratePpb));

            push({ (mRef.count + counts) & skCounterMask, at }, nowUs, ratePpb);
        }
    }

    /// Forget the tie between the counter and the system clock: the streams
    /// stopped, and nothing says how the counter ran meanwhile. The rates
    /// are the part's oscillators against the system clock, which a stop
    /// does not change: the next fit starts from them.
    void reset()
    {
        mHave    = false;
        mFitting = false;
    }

    /// The counter is 24 bits wide.
    static constexpr uint32_t skCounterMask = 0x00FFFFFFU;

    /// How far under the hand-over the newest frame of a stream's first read
    /// is put. Nothing is known of how late a hand-over comes until several
    /// have been seen, and a line started too high has to come down against
    /// the read's bound, which squeezes frames together.
    static constexpr uint32_t skFirstLeadUs = 3000;

    /// How far under the earliest of the last hand-overs the line is held:
    /// room for the grain of the clock the hand-overs are read off.
    static constexpr uint32_t skLeadUs = 1000;

    /// How much further under them the line is held for a read that is over
    /// sooner after its newest frame than the ones kept were, as when a
    /// stream leaves the FIFO and there are fewer bytes to read.
    static constexpr uint32_t skShorterReadUs = 1000;

    /// How many reads the earliest hand-over is looked for in.
    static constexpr uint32_t skEnvelopeReads = 32;

    /// The share of the line's distance from where it should be that is
    /// closed a read, and the ceiling on it as a fraction of the time the
    /// read covers: what an interval between two frames may be off by.
    static constexpr uint32_t skSlewShare          = 32;
    static constexpr uint32_t skSlewCeilingDivisor = 1024;

    /// How many reads the rate is fitted over: a read's weight decays by
    /// 1/skFitReads a read after it. Long enough that the scatter of the
    /// hand-overs averages out, short enough to follow an oscillator as it
    /// warms.
    static constexpr uint32_t skFitReads = 64;

    /// The fit's prior. An oscillator the base has not seen yet is taken to
    /// run true, held to the part's tolerance against hand-overs this
    /// scattered; one it has seen starts from the rate fitted then, held no
    /// tighter than the oscillator may have drifted since.
    static constexpr uint32_t skHandOverNoiseUs = 1500;
    static constexpr uint32_t skRatePriorPpm    = 10000;
    static constexpr uint32_t skRateKeptPpm     = 50;

    /// A hand-over this much later than the one before it, over what the
    /// widest rate would explain, is a read that was held up, or a batch
    /// whose stream had stopped before the read: left out of the fit. The
    /// frames get their times all the same.
    static constexpr uint32_t skHeldUpUs = 8000;

    /// The widest the fitted rate is let go, in parts per billion: past the
    /// part's tolerance.
    static constexpr int64_t skRateLimitPpb = 30000000LL;

    /// How far behind the last reference a new read's frame has to stand to
    /// be taken for the counter having started anew: ten seconds of counts.
    /// Less than that may be a reference set ahead of the frames, as the one
    /// at a switch of the gyroscope is: it stands ahead of the frames still
    /// in the FIFO by as long as they have waited there, which is a period
    /// of the slowest rate or the time between two reads at the most.
    static constexpr uint32_t skRestartCounts = 256000;

    /// The longest two references may be apart. Past it the counter, which
    /// wraps every 655 s, cannot say how far it ran.
    static constexpr uint64_t skLongestGapUs = 160ULL * 1000ULL * 1000ULL;

private:

    static constexpr int64_t kPpb = 1000000000LL;

    /// A place on the line: a counter value and the system time given to it.
    struct Reference {
        uint32_t count = 0;
        uint64_t us    = 0;
    };

    /// An oscillator's rate against the system clock, positive when the
    /// counter is slow, and how much the fit that gave it knew.
    struct ClockRate {
        double rate   = 0.0;
        double weight = 0.0;
    };

    /// The line since the fit's origin, in microseconds from it.
    struct Line {
        /// The counted time, what the rate has added to it, and how far the
        /// line has been moved to sit under the hand-overs.
        double countedUs = 0.0;
        double skewUs    = 0.0;
        double offsetUs  = 0.0;

        /// The fit: the decayed weight of its reads, the means of the
        /// counted time and of the hand-overs' distance from it, their
        /// decayed deviations, the rate the fit started from with its
        /// weight, and the rate it gives.
        double    weight      = 0.0;
        double    meanX       = 0.0;
        double    meanZ       = 0.0;
        double    varX        = 0.0;
        double    covXZ       = 0.0;
        double    prior       = 0.0;
        double    priorWeight = 0.0;
        ClockRate fitted {};

        /// The last hand-over's distance from the counted time, and how far
        /// above the line the last hand-overs came.
        double   lastZ = 0.0;
        int32_t  late[skEnvelopeReads] {};
        uint32_t lateCount = 0;
        uint32_t lateNext  = 0;
    };

    /// How far one counter value is ahead of another, across the wrap.
    static int32_t ahead(uint32_t count, uint32_t of)
    {
        const uint32_t delta = (count - of) & skCounterMask;

        if (delta > (skCounterMask / 2U)) {
            return static_cast<int32_t>(delta) - static_cast<int32_t>(skCounterMask) - 1;
        }

        return static_cast<int32_t>(delta);
    }

    /// Counts as microseconds of the system clock, at a rate. One count is
    /// exactly 625/16 us of the part's own time.
    static int64_t countsToUs(int64_t counts, int64_t ratePpb)
    {
        const int64_t sixteenths = counts * 625;

        return (sixteenths + ((sixteenths * ratePpb) / kPpb)) / 16;
    }

    static uint32_t usToCounts(uint64_t us, int64_t ratePpb)
    {
        return static_cast<uint32_t>((us * 16U * static_cast<uint64_t>(kPpb))
                                     / (625U * static_cast<uint64_t>(kPpb + ratePpb)));
    }

    static int64_t toPpb(double rate)
    {
        return static_cast<int64_t>(rate * static_cast<double>(kPpb));
    }

    static int64_t rounded(double value)
    {
        return static_cast<int64_t>((value < 0.0) ? (value - 0.5) : (value + 0.5));
    }

    /// The weight of a rate known to so many parts per million, against
    /// hand-overs as scattered as they are.
    static constexpr double priorWeight(uint32_t ppm)
    {
        return (static_cast<double>(skHandOverNoiseUs) * static_cast<double>(skHandOverNoiseUs) * 1e12)
             / (static_cast<double>(ppm) * static_cast<double>(ppm));
    }

    /// Tie the counter to the system clock at the first read of a stream.
    void anchor(uint32_t sensorTime, uint64_t nowUs)
    {
        const uint64_t at = (nowUs > skFirstLeadUs) ? (nowUs - skFirstLeadUs) : 0U;

        mRef        = { sensorTime, at };
        mPrev       = mRef;
        mRefNowUs   = nowUs;
        mBeforePpb  = mRatePpb;
        mSegmentPpb = mRatePpb;
        mHave       = true;
        mClockAhead = false;

        startFit(nowUs, false);
    }

    /// The first batch after the oscillator changed: on from the last
    /// reference at the new oscillator's rate, and a fit of its own. How
    /// late the hand-overs come has not changed with the oscillator, so the
    /// new line is told how far above the frames' times the last ones came.
    void resume(uint32_t sensorTime, uint64_t nowUs)
    {
        const int32_t moved = static_cast<int32_t>(rounded(mLine.offsetUs));

        mUndo = mLine;

        for (uint32_t i = 0; i < mUndo.lateCount; i++) {
            mUndo.late[i] -= moved;
        }

        push({ sensorTime, bounded(stamp(sensorTime, UINT64_MAX), nowUs, mRef.us) }, nowUs, mRatePpb);
        startFit(nowUs, true);
    }

    /// A batch of the read just taken in, with a newer frame than the one
    /// the read was taken in by: the read is taken in anew by this one.
    void refine(uint32_t sensorTime, uint64_t nowUs)
    {
        if (mReadIsOrigin && !mReadCarries) {
            // The read the counter is tied at: it is this frame that is the
            // first lead under the hand-over. There is no room for it over
            // the one tied at before, and frames squeezed in would be many
            // where moving the tie shortens one interval of the other stream.
            // Frames of the same stream as the batch tied at before - a
            // stretch at a new rate in the very first read - have no times
            // left for them either way and come out bunched.
            anchor(sensorTime, nowUs);

            return;
        }

        if (mReadIsOrigin) {
            mRef = { sensorTime, bounded(stamp(sensorTime, UINT64_MAX), nowUs, mPrev.us) };

            startFit(nowUs, true);

            return;
        }

        mLine = mUndo;

        advance(sensorTime, ahead(sensorTime, mReadFrom.count), nowUs, true);
    }

    /// Hold a reference's time no later than its hand-over and no earlier
    /// than the reference before it.
    static uint64_t bounded(uint64_t us, uint64_t nowUs, uint64_t floorUs)
    {
        if (us > nowUs) {
            us = (nowUs > skLeadUs) ? (nowUs - skLeadUs) : 0U;
        }

        return (us > floorUs) ? us : floorUs;
    }

    /// Whether the system clock stepped: the hand-over stands off the line
    /// by more than the part's tolerance over the step can explain.
    ///
    /// Behind the line, it stepped: nothing else puts a hand-over before
    /// its frame. Ahead of it, the read may only have been held up, which
    /// says nothing of the clock: the line stays, and the read's frames
    /// stand on it where they belong. A clock that stepped on is still
    /// ahead at the next read, and that one is taken for the step.
    bool clockStepped(int32_t step, uint64_t nowUs)
    {
        constexpr int64_t kClockStepUs = 1000000;

        const int64_t counted = countsToUs(step, 0);
        const int64_t onLine  = static_cast<int64_t>(mRef.us) + countsToUs(step, mRatePpb);
        const int64_t late    = static_cast<int64_t>(nowUs) - onLine;
        const int64_t limit   = kClockStepUs + (counted / 16);

        if (late < -limit) {
            return true;
        }

        const bool ahead = (late > limit);
        const bool again = ahead && mClockAhead;

        mClockAhead = ahead;

        return again;
    }

    /// Start the fit at the last reference: the line runs from it.
    /// @param carry: The line before it, kept aside, says how late the
    ///               hand-overs come.
    void startFit(uint64_t nowUs, bool carry)
    {
        const ClockRate& clock = mRate[mClock];

        double weight = clock.weight;

        if (weight < priorWeight(skRatePriorPpm)) {
            weight = priorWeight(skRatePriorPpm);
        } else if (weight > priorWeight(skRateKeptPpm)) {
            weight = priorWeight(skRateKeptPpm);
        }

        const double late = static_cast<double>(nowUs - mRef.us);

        mLine             = Line {};
        mLine.prior       = clock.rate;
        mLine.priorWeight = weight;
        mLine.fitted      = clock;
        mLine.weight      = 1.0;
        mLine.meanZ       = late;
        mLine.lastZ       = late;

        if (carry) {
            for (uint32_t i = 0; i < mUndo.lateCount; i++) {
                mLine.late[i] = mUndo.late[i];
            }

            mLine.lateCount = mUndo.lateCount;
            mLine.lateNext  = mUndo.lateNext;
        }

        noteLate(static_cast<int32_t>(nowUs - mRef.us));

        mFitting      = true;
        mOriginUs     = mRef.us;
        mReadNowUs    = nowUs;
        mReadIsOrigin = true;
        mReadCarries  = carry;
    }

    /// Keep how far above the line a hand-over came, in place of the oldest
    /// kept.
    void noteLate(int32_t late)
    {
        mLine.late[mLine.lateNext] = late;
        mLine.lateNext             = (mLine.lateNext + 1U) % skEnvelopeReads;

        if (mLine.lateCount < skEnvelopeReads) {
            mLine.lateCount++;
        }
    }

    /// A read of a running fit: take it into the rate, carry the line on,
    /// move it towards its place under the hand-overs, and set the
    /// reference on it.
    /// @param step:    The counts since the reference the read started from.
    /// @param replace: The read has a reference already, which this one is
    ///                 to take the place of.
    void advance(uint32_t sensorTime, int32_t step, uint64_t nowUs, bool replace)
    {
        const double stepUs = (static_cast<double>(step) * 625.0) / 16.0;
        const double now    = static_cast<double>(nowUs - mOriginUs);

        mLine.countedUs += stepUs;

        // The line is carried on at the rate fitted up to the read before,
        // so where a batch is put does not depend on how late it was handed
        // over itself: a batch of the same read with an older newest frame
        // is put on the same line.
        mLine.skewUs += mLine.fitted.rate * stepUs;

        fit(now, stepUs);

        mRate[mClock] = mLine.fitted;
        mRatePpb      = toPpb(mLine.fitted.rate);

        // The line by the rate alone, and how far above it this hand-over is.
        const double line = mLine.countedUs + mLine.skewUs;

        // A read held up for longer than the window's entries can say is
        // kept as the longest they can.
        const int64_t late = rounded(now - line);

        noteLate((late > INT32_MAX) ? INT32_MAX : static_cast<int32_t>(late));

        int32_t earliest = mLine.late[0];

        for (uint32_t i = 1; i < mLine.lateCount; i++) {
            if (mLine.late[i] < earliest) {
                earliest = mLine.late[i];
            }
        }

        const double want  = static_cast<double>(earliest) - static_cast<double>(skLeadUs + skShorterReadUs);
        const double limit = stepUs / static_cast<double>(skSlewCeilingDivisor);

        double move = (want - mLine.offsetUs) / static_cast<double>(skSlewShare);

        if (move > limit) {
            move = limit;
        } else if (move < -limit) {
            move = -limit;
        }

        mLine.offsetUs += move;

        // Later than its own hand-over: the one case the line is stepped.
        if ((line + mLine.offsetUs) > now) {
            mLine.offsetUs = now - static_cast<double>(skLeadUs) - line;
        }

        const int64_t  at      = static_cast<int64_t>(mOriginUs) + rounded(line + mLine.offsetUs);
        const uint64_t floorUs = replace ? mPrev.us : mRef.us;

        uint64_t stamped = (at > 0) ? static_cast<uint64_t>(at) : 0U;

        if (stamped < floorUs) {
            stamped = floorUs;
        }

        if (replace) {
            mRef = { sensorTime, stamped };
        } else {
            push({ sensorTime, stamped }, nowUs, mRatePpb);
        }
    }

    /// Take this hand-over into the rate: a least-squares slope of the
    /// hand-overs against the counted time, over weights that decay by read,
    /// and drawn towards the prior while the reads are few. Recursive, so
    /// the sums are kept as deviations from their means and stay small on a
    /// long stream.
    void fit(double now, double stepUs)
    {
        constexpr double kDecay = 1.0 - (1.0 / static_cast<double>(skFitReads));
        constexpr double kLimit = static_cast<double>(skRateLimitPpb) / static_cast<double>(kPpb);

        // x: the counted time since the fit's origin; z: how far the
        // hand-over sits from it, whose slope against x is the rate.
        const double x = mLine.countedUs;
        const double z = now - x;

        const double later = z - mLine.lastZ;

        mLine.lastZ = z;

        if (later > (static_cast<double>(skHeldUpUs) + (stepUs * kLimit))) {
            return;
        }

        mLine.weight = (mLine.weight * kDecay) + 1.0;

        const double dx = x - mLine.meanX;
        mLine.meanX += dx / mLine.weight;
        mLine.meanZ += (z - mLine.meanZ) / mLine.weight;

        mLine.varX  = (mLine.varX * kDecay) + (dx * (x - mLine.meanX));
        mLine.covXZ = (mLine.covXZ * kDecay) + (dx * (z - mLine.meanZ));

        // The prior fades with the reads, or it would hold the rate where an
        // oscillator that has since warmed or cooled once was.
        mLine.priorWeight *= kDecay;

        const double weight = mLine.varX + mLine.priorWeight;

        if (!(weight > 0.0)) {
            return;
        }

        double rate = (mLine.covXZ + (mLine.priorWeight * mLine.prior)) / weight;

        if (rate > kLimit) {
            rate = kLimit;
        } else if (rate < -kLimit) {
            rate = -kLimit;
        }

        mLine.fitted = { rate, weight };
    }

    /// Set a new reference; the one before it becomes the far end of the
    /// stretch frames are spaced over.
    /// @param ratePpb: The rate the stretch up to the new reference ran at.
    void push(const Reference& reference, uint64_t nowUs, int64_t ratePpb)
    {
        mBeforePpb  = mSegmentPpb;
        mSegmentPpb = ratePpb;
        mPrev       = mRef;
        mRef        = reference;
        mRefNowUs   = nowUs;
    }

    /// The last two references, the system time the last was set at, and
    /// the rates of the stretch between them and of the one before it.
    Reference mPrev {};
    Reference mRef {};
    uint64_t  mRefNowUs   = 0;
    int64_t   mBeforePpb  = 0;
    int64_t   mSegmentPpb = 0;
    bool      mHave       = false;

    /// The last read was handed over far ahead of the line: held up, or the
    /// first after the system clock stepped on.
    bool mClockAhead = false;

    /// The oscillator the counter runs on, 0 with the gyroscope off and 1
    /// with it on, the rate of each, and the running one's in parts per
    /// billion for the frames.
    uint8_t   mClock = 0;
    ClockRate mRate[2] {};
    int64_t   mRatePpb = 0;

    /// The line and where it starts. Unset between a switch of the
    /// oscillator and the first batch after it.
    bool     mFitting  = false;
    uint64_t mOriginUs = 0;
    Line     mLine {};

    /// The read being taken in, for a batch of the same read with a newer
    /// frame: its hand-over and the line and the reference it started from.
    /// Where the fit starts at the read, the line kept is the one before
    /// it, if that one's hand-overs are carried over.
    uint64_t  mReadNowUs    = 0;
    bool      mReadIsOrigin = false;
    bool      mReadCarries  = false;
    Line      mUndo {};
    Reference mReadFrom {};

};

#endif // COMPONENT_IMU_TIME_BASE_HPP
