/**
 ******************************************************************************
 * @file    SourceStepTrack.hpp
 * @brief   Which sampling step each sample held in a queue was taken at
 *
 ******************************************************************************
 */

#ifndef SENSOR_SOURCE_STEP_TRACK_HPP
#define SENSOR_SOURCE_STEP_TRACK_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Sensor
{

/**
 * @brief The sampling step of a source, by the time its samples were taken.
 *
 * A driver reports one step, and its listeners count the samples it publishes
 * against it. A sensor that holds samples in a queue between taking them and
 * publishing them cannot report the step when it takes them: a sample taken
 * before a change of rate would be published under the step of after it, and
 * counted wrongly. It notes here the step with the stamp of the first sample
 * taken at it, and when it publishes a sample asks for the step by that
 * sample's stamp.
 *
 * The last few changes are kept. A sample older than all of them gets the
 * oldest step kept: the queue would have to hold samples of more rates than
 * that at once. Stamps that start over leave nothing kept but the step they
 * come with.
 *
 * Noting is one thread's at a time, with no lock between it and those that
 * ask, of which there can be several. A sensor that notes from a listener's
 * callback has that from the driver delivering to it, which holds its lock
 * across the call. An answer taken while a change is being noted is the
 * newest step: that can be wrong for a sample from before the change, once,
 * and cannot hold any thread up.
 */
class SourceStepTrack
{
public:
    static constexpr size_t skDepth = 4;

    /**
     * @brief Samples stamped from here on were taken at this step.
     * @param fromUs Stamp of the first sample at the step, us.
     * @param stepUs The step, us; 0 for none.
     * @note  A step that is the newest one already is not noted again, so
     *        this can be called for every batch.
     */
    void note(uint64_t fromUs, uint32_t stepUs)
    {
        const uint32_t count = mCount.load(std::memory_order_relaxed);

        // A stamp behind the newest change is a stream that started its
        // stamps over: nothing kept says anything about it, the same step
        // or another.
        const bool startedOver = (count != 0) && (fromUs < from(count - 1));

        if ((count != 0) && !startedOver
                && (mStep[count - 1].load(std::memory_order_relaxed) == stepUs)) {
            return;
        }

        mNewest.store(stepUs, std::memory_order_relaxed);
        mSeq.fetch_add(1, std::memory_order_acq_rel);

        uint32_t kept = startedOver ? 0 : count;

        if (kept == skDepth) {
            for (uint32_t i = 1; i < skDepth; ++i) {
                mFromLo[i - 1].store(mFromLo[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                mFromHi[i - 1].store(mFromHi[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                mStep[i - 1].store(mStep[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            --kept;
        }

        mFromLo[kept].store(static_cast<uint32_t>(fromUs), std::memory_order_relaxed);
        mFromHi[kept].store(static_cast<uint32_t>(fromUs >> 32), std::memory_order_relaxed);
        mStep[kept].store(stepUs, std::memory_order_relaxed);
        mCount.store(kept + 1, std::memory_order_relaxed);

        mSeq.fetch_add(1, std::memory_order_acq_rel);
    }

    /**
     * @brief The step a sample was taken at.
     * @param tsUs The sample's stamp, us.
     * @return The step, us; 0 when none was noted.
     */
    uint32_t stepAt(uint64_t tsUs) const
    {
        const uint32_t before = mSeq.load(std::memory_order_acquire);

        if ((before & 1U) != 0U) {
            return mNewest.load(std::memory_order_relaxed);
        }

        const uint32_t count = mCount.load(std::memory_order_relaxed);
        uint32_t       step  = 0;

        // The newest change at or before the sample; the oldest one kept
        // for a sample from before them all.
        for (uint32_t i = 0; i < count; ++i) {
            if ((i == 0) || (from(i) <= tsUs)) {
                step = mStep[i].load(std::memory_order_relaxed);
            }
        }

        // The loads above are relaxed, and may not be moved past this
        // re-check: the fence holds them ahead of it. On the one core of the
        // part only the compiler could move them.
        std::atomic_thread_fence(std::memory_order_acquire);

        if (mSeq.load(std::memory_order_relaxed) != before) {
            return mNewest.load(std::memory_order_relaxed);
        }

        return step;
    }

private:
    uint64_t from(uint32_t i) const
    {
        return (static_cast<uint64_t>(mFromHi[i].load(std::memory_order_relaxed)) << 32)
             | mFromLo[i].load(std::memory_order_relaxed);
    }

    // The changes kept, oldest first. The stamps are in halves: a 64-bit
    // atomic is not lock-free on the part.
    std::atomic<uint32_t> mFromLo[skDepth] {};
    std::atomic<uint32_t> mFromHi[skDepth] {};
    std::atomic<uint32_t> mStep[skDepth] {};
    std::atomic<uint32_t> mCount { 0 };
    std::atomic<uint32_t> mNewest { 0 };    ///< The step noted last
    std::atomic<uint32_t> mSeq { 0 };       ///< Odd while a change is being noted
};

} // namespace Sensor

#endif // SENSOR_SOURCE_STEP_TRACK_HPP
