/**
 ******************************************************************************
 * @file    FifoGrid.hpp
 * @brief   The instants, on the BMI270's own counter, of the frames a stream
 *          has in a FIFO read.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_BMI270_FIFO_GRID_HPP
#define HARDWARE_BMI270_FIFO_GRID_HPP

#include <cstdint>

#include "Hardware/BMI270/Bmi270Odr.hpp"

namespace Hardware::Bmi270Fifo
{

/// The part's counter is 24 bits wide.
constexpr uint32_t kCounterMask = 0x00FFFFFF;

/// Whether counter value 'a' is later than 'b': ahead of it by less than
/// half the counter's range.
inline bool isAfter(uint32_t a, uint32_t b)
{
    const uint32_t ahead = (a - b) & kCounterMask;

    return (ahead != 0) && (ahead < ((kCounterMask + 1) / 2));
}

/**
 * @brief Where one stream's frames stand on the part's counter.
 *
 * The part takes every reading at a counter value that is a whole multiple
 * of the stream's period. It does not store that value with the reading; a
 * read that empties the FIFO ends with the counter as it then stood, and
 * the newest reading is the last multiple at or before that. The rest stand
 * one period apart behind it.
 *
 * That holds for a stream that runs. One whose rate was written anew goes on
 * at the old rate for a while, then stops, and starts at the new rate only
 * when the part says so: its last frames at the old rate may stand well
 * before the end of the read. They follow the frames before them without a
 * break, so they are placed forward from those.
 *
 * Arithmetic only. The values given out for a stream only ever increase: a
 * frame that could not be given a later value than the one before it is
 * left out.
 */
class StreamGrid
{
public:
    /// The frames of a batch that are given out, and where they stand.
    struct Batch {
        uint32_t first   = 0;   ///< Counter value of the first frame given out
        uint32_t step    = 0;   ///< Counts from a frame to the next
        uint16_t count   = 0;   ///< Frames given out
        uint16_t dropped = 0;   ///< Frames left out, from the start of the batch
    };

    /// Forget where the stream stood: the counter started anew, or the
    /// stream comes into the FIFO from outside it.
    void restart()
    {
        mHaveLast  = false;
        mHaveFloor = false;
    }

    /// Forget where the part said the next frame stands: frames were lost
    /// between there and the ones that came.
    void forgetFloor()
    {
        mHaveFloor = false;
    }

    /// The rate the frames to come were taken at, as the register value.
    /// @param ran Whether the stream ran at the rate it had till now. One
    ///            that was switched off has no rate to go on at.
    void setOdr(uint8_t odr, bool ran = true)
    {
        if (!ran) {
            mWasOdr = 0;
            mOdr    = odr;
        } else if (odr != mOdr) {
            mWasOdr = mOdr;
            mOdr    = odr;
        }
    }

    uint8_t odr() const
    {
        return mOdr;
    }

    /// Counts from a frame to the next at the stream's rate, 0 with no rate set.
    uint32_t step() const
    {
        return Bmi270Odr::periodCounts(mOdr);
    }

    /// The part said the next frame it stores is taken at this counter value:
    /// no frame of the stream that follows stands before it.
    void notBefore(uint32_t sensorTime)
    {
        mFloor     = sensorTime & kCounterMask;
        mHaveFloor = true;
    }

    /**
     * @brief The rate a stretch's frames were taken at, read off the frames.
     *
     * The part names the counter value of the first frame after a change of
     * settings, and the stretch ends by a known value. Frames stand on the
     * multiples of their period, so the rate is the one with as many
     * multiples from the one to the other as frames came. That holds
     * whatever was written to the part meanwhile: a stream that came into
     * the FIFO still running at the rate it had before, or a rate written
     * again before the part's word of the first one was read.
     *
     * Where several rates fit, as with one frame in a short stretch, the
     * frames do not say. A stream nothing was placed of yet, which has just
     * come into the FIFO, is then taken to run at the rate it had before the
     * last one set, if that fits.
     *
     * @param count Frames of the stream in the stretch.
     * @param end   A counter value the stretch's frames stand no later than.
     * @return The rate, or 0 when the frames do not say: the part named no
     *         start for the stretch, no rate fits, or several do.
     */
    uint8_t rateOf(uint16_t count, uint32_t end) const
    {
        if (!mHaveFloor || count == 0) {
            return 0;
        }

        const uint32_t span = (end - mFloor) & kCounterMask;

        if (span > (kCounterMask / 2u)) {
            return 0;
        }

        uint8_t fits = 0;
        uint8_t only = 0;
        bool    was  = false;

        for (uint8_t odr = Bmi270Odr::kSlowest; odr <= Bmi270Odr::kFastest; ++odr) {
            const uint32_t period = Bmi270Odr::periodCounts(odr);
            const uint32_t first  = (period - (mFloor & (period - 1u))) & (period - 1u);
            const uint32_t frames = (first > span) ? 0u : (((span - first) / period) + 1u);

            if (frames != count) {
                continue;
            }

            ++fits;
            only = odr;
            was |= (odr == mWasOdr);
        }

        if (fits == 1) {
            return only;
        }

        return (was && !mHaveLast) ? mWasOdr : 0;
    }

    /**
     * @brief Whether a batch that ends at a read follows the last frame given
     *        out with none missing and none over, at the stream's rate.
     * @param count          Frames in the batch.
     * @param readSensorTime The counter when the read emptied the FIFO.
     */
    bool continues(uint16_t count, uint32_t readSensorTime) const
    {
        const uint32_t period = step();

        if (!mHaveLast || period == 0) {
            return false;
        }

        return newestAt(readSensorTime, period) == ((mLast + count * period) & kCounterMask);
    }

    /**
     * @brief Whether frames that came for a stream still held to its old
     *        rate can only have been taken at the new one.
     *
     * The part says in the FIFO when a new rate takes effect. Should that
     * word be lost, the frames tell: they continue the stream at the new
     * rate; or there are more of them than the old rate has room for up to
     * the read; or they come long after the old rate's frames stopped, when
     * the part has had time to start the new one twice over.
     *
     * @param writtenOdr     The rate written to the part.
     * @param count          Frames that came.
     * @param readSensorTime The counter when the read emptied the FIFO.
     */
    bool outgrew(uint8_t writtenOdr, uint16_t count, uint32_t readSensorTime) const
    {
        const uint32_t period    = step();
        const uint32_t newPeriod = Bmi270Odr::periodCounts(writtenOdr);

        if (count == 0 || period == 0 || newPeriod == 0 || writtenOdr == mOdr) {
            return false;
        }

        // Nothing given out to go on from: the old rate has no hold on them.
        if (!mHaveLast) {
            return true;
        }

        StreamGrid written = *this;
        written.setOdr(writtenOdr);

        if (written.continues(count, readSensorTime)) {
            return true;
        }

        const uint32_t read = readSensorTime & kCounterMask;
        const uint32_t end  = (((mLast + period) & ~(period - 1u)) + (count - 1u) * period) & kCounterMask;

        // How far before the read the frames would end at the old rate. An
        // end past the read wraps to a count no wait comes near.
        return ((read - end) & kCounterMask) > 2u * (period + newPeriod);
    }

    /**
     * @brief Place a batch that ends at a read which emptied the FIFO.
     * @param count          Frames in the batch.
     * @param readSensorTime The counter when the read emptied the FIFO.
     */
    Batch layBack(uint16_t count, uint32_t readSensorTime)
    {
        Batch          batch {};
        const uint32_t period = step();

        mHaveFloor = false;

        if (count == 0 || period == 0) {
            batch.dropped = count;
            return batch;
        }

        const uint32_t newest = newestAt(readSensorTime, period);
        uint32_t       first  = (newest - (count - 1u) * period) & kCounterMask;
        uint32_t       drop   = 0;

        if (mHaveLast && !isAfter(first, mLast)) {
            drop = isAfter(newest, mLast) ? (((mLast - first) & kCounterMask) / period) + 1u : count;
        }

        batch.step    = period;
        batch.dropped = static_cast<uint16_t>(drop);
        batch.count   = static_cast<uint16_t>(count - drop);
        batch.first   = (first + drop * period) & kCounterMask;

        if (batch.count != 0) {
            mLast     = newest;
            mHaveLast = true;
        }

        return batch;
    }

    /**
     * @brief Place a batch that goes on from the last frame given out: the
     *        frames before a change of the stream's rate, or those of a read
     *        that did not reach the end of the FIFO.
     * @param count Frames in the batch.
     */
    Batch layForward(uint16_t count)
    {
        Batch          batch {};
        const uint32_t period = step();
        const bool     floor  = mHaveFloor;

        mHaveFloor = false;

        if (count == 0 || period == 0 || (!mHaveLast && !floor)) {
            batch.dropped = count;
            return batch;
        }

        // The first multiple of the period after the last frame, and not
        // before what the part said the next frame stands at.
        uint32_t from = mHaveLast ? ((mLast + 1u) & kCounterMask) : mFloor;

        if (mHaveLast && floor && isAfter(mFloor, from)) {
            from = mFloor;
        }

        batch.first = ((from + period - 1u) & ~(period - 1u)) & kCounterMask;
        batch.step  = period;
        batch.count = count;

        mLast     = (batch.first + (count - 1u) * period) & kCounterMask;
        mHaveLast = true;

        return batch;
    }

private:
    /// The last multiple of the period at or before a counter value.
    static uint32_t newestAt(uint32_t sensorTime, uint32_t period)
    {
        return (sensorTime & kCounterMask) & ~(period - 1u);
    }

    uint8_t  mOdr       = 0;
    uint8_t  mWasOdr    = 0;        ///< The rate before the one set last
    uint32_t mLast      = 0;        ///< Counter value of the last frame given out
    bool     mHaveLast  = false;
    uint32_t mFloor     = 0;        ///< What the next frame stands no earlier than
    bool     mHaveFloor = false;
};

/**
 * @brief Place a stream's frames of one stretch of a read on the part's counter.
 *
 * A stream that runs at the rate written to the part is counted back from
 * where the stretch is known to end. One that still waits for the part
 * to say its new rate has taken effect goes on from its last frame: its
 * frames at the old rate stop some time before the new ones start, and a
 * read may end in between. If the part's word was lost - as when the FIFO is
 * set up anew just as the rate changes - the frames themselves show it, and
 * the stream takes the new rate. A stream that was switched off goes on from
 * its last frame too: what the FIFO still held of it ends where it stopped,
 * not where the read does.
 *
 * A stretch that starts where the part named its next frame has its rate
 * read off its frames and is laid on from there. Not so for a stream that
 * waits for its new rate: its frames stop short of the stretch's end, and
 * would pass for a slower rate.
 *
 * A stretch the part dropped frames before, the FIFO having filled up, is no
 * guide to where its frames stand unless it runs up to an end that is known:
 * with no end, or for a stream that was switched off or waits for its new
 * rate, the stream starts anew, and the frames, which have no place, are
 * left out.
 *
 * @param grid       The stream.
 * @param writtenOdr The rate written to the part for it.
 * @param live       Whether the stream is on.
 * @param count      Frames of the stream in the stretch.
 * @param endTime    A counter value the stretch is known to end by; nullptr
 *                   when there is none.
 * @param skipped    Whether the part said it dropped frames before the
 *                   stretch's.
 */
inline StreamGrid::Batch placeFrames(StreamGrid& grid, uint8_t writtenOdr, bool live, uint16_t count,
                                     const uint32_t* endTime, bool skipped = false)
{
    const bool waits = (grid.odr() != writtenOdr);

    if (skipped) {
        if (endTime == nullptr || !live || waits) {
            grid.restart();

            return grid.layForward(count);
        }

        grid.forgetFloor();
    }

    if (endTime == nullptr || !live) {
        return grid.layForward(count);
    }

    const uint8_t taken = waits ? 0 : grid.rateOf(count, *endTime);

    if (taken != 0) {
        grid.setOdr(taken);

        return grid.layForward(count);
    }

    if (waits) {
        if (!grid.outgrew(writtenOdr, count, *endTime)) {
            return grid.layForward(count);
        }

        grid.setOdr(writtenOdr);
    }

    return grid.layBack(count, *endTime);
}

} // namespace Hardware::Bmi270Fifo

#endif // HARDWARE_BMI270_FIFO_GRID_HPP
