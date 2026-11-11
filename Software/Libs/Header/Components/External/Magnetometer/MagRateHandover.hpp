/**
 ******************************************************************************
 * @file    MagRateHandover.hpp
 * @brief   Which rate a magnetometer sample was converted at, around a change
 *          of rate made while the part converts.
 *
 *          The decision only: no clock, bus or thread, so it can be checked
 *          without a part or an RTOS.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_RATE_HANDOVER_HPP
#define COMPONENT_MAG_RATE_HANDOVER_HPP

#include <cstdint>

/**
 * @brief   The period a sample was converted at.
 *
 *          The part takes a new rate while it converts, but not at once: the
 *          conversion in flight is finished at the old rate, one old period
 *          after the one before it, and only then does the part convert at
 *          the new one. Going to a faster rate, that is up to a whole old
 *          period after the rate was written. A sample that carries on the
 *          old grid after a change is therefore of the old rate, and the
 *          first one that does not is where the new rate starts.
 */
class MagRateHandover
{

public:

    /**
     * @brief   A stream starts: nothing is carried over from the last one.
     */
    void start()
    {
        mOldUs    = 0;
        mHaveLast = false;
    }

    /**
     * @brief   The part was given another rate while converting.
     * @param   oldPeriodUs: The period it was converting at.
     * @note    A second change before the first has shown in the samples
     *          leaves the conversion in flight where it was, at the rate
     *          before the first.
     */
    void changed(uint32_t oldPeriodUs)
    {
        if (mOldUs == 0U) {
            mOldUs = oldPeriodUs;
        }
    }

    /**
     * @brief   The period a sample was converted at.
     * @param   atUs: The sample's stamp, us. Called for every sample of the
     *          stream, in order.
     * @param   appliedUs: The period the part is set to now.
     * @retval  The old period for a sample that carries on the old grid
     *          after a change, the applied one otherwise.
     */
    uint32_t periodFor(uint64_t atUs, uint32_t appliedUs)
    {
        uint32_t periodUs = appliedUs;

        if (mOldUs != 0U) {
            if (mHaveLast && (atUs > mLastUs) && onGrid(atUs - mLastUs, mOldUs)) {
                periodUs = mOldUs;
            } else {
                mOldUs = 0;
            }
        }

        mLastUs   = atUs;
        mHaveLast = true;

        return periodUs;
    }

private:

    /// Whether a step is one period of a grid, a quarter either way: the
    /// rates of the part are a factor of two apart or more. Read on the part
    /// with 100 Hz as one of the two rates, the step it takes into a new
    /// rate was not one of the old.
    static bool onGrid(uint64_t stepUs, uint32_t periodUs)
    {
        return ((stepUs * 4U) >= (static_cast<uint64_t>(periodUs) * 3U))
            && ((stepUs * 4U) <= (static_cast<uint64_t>(periodUs) * 5U));
    }

    uint32_t mOldUs    = 0;         ///< Period before a change not yet seen in the samples
    uint64_t mLastUs   = 0;         ///< Stamp of the sample before
    bool     mHaveLast = false;

};

#endif // COMPONENT_MAG_RATE_HANDOVER_HPP
