/**
 ******************************************************************************
 * @file    LatencyAggregate.hpp
 * @brief   The delivery latency a driver owes to all its listeners at once.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_LATENCY_AGGREGATE_HPP
#define SENSOR_LATENCY_AGGREGATE_HPP

#include <cstdint>

namespace Sensor
{

/**
 * @brief Tightest latency among the listeners added to it.
 *
 * A listener's latency is how late a reading may reach it, in ms, so the
 * shortest one binds the source. Zero is a request too - a listener faster
 * than a millisecond ends up with it - and means "as soon as possible", so it
 * is counted as 1 ms rather than mistaken for "nothing asked".
 */
class LatencyAggregate
{
public:
    void add(uint32_t latencyMs)
    {
        const uint32_t wanted = (latencyMs == 0) ? 1 : latencyMs;

        if (mValue == 0 || wanted < mValue) {
            mValue = wanted;
        }
    }

    /// Tightest latency added so far, 0 when nothing was added.
    uint32_t value() const
    {
        return mValue;
    }

private:
    uint32_t mValue = 0;
};

} // namespace Sensor

#endif // SENSOR_LATENCY_AGGREGATE_HPP
