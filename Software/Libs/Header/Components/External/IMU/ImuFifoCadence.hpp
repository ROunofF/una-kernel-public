/**
 ******************************************************************************
 * @file    ImuFifoCadence.hpp
 * @brief   How often the IMU FIFO is read, from what its streams ask for.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_IMU_FIFO_CADENCE_HPP
#define COMPONENT_IMU_FIFO_CADENCE_HPP

#include <cstdint>

namespace ImuFifoCadence
{

/// Shortest time between FIFO reads. Every read wakes the system, so a
/// stream asking for less is held to this.
constexpr uint32_t kMinFifoLatencyMs = 100;

/// Time between FIFO reads when no running stream asks for anything, and the
/// longest one that is honoured.
constexpr uint32_t kDefaultFifoLatencyMs = 1000;

/// How late a FIFO read may come after the part asks for it: the longest the
/// reading thread is taken to be held up. The FIFO is kept room for what the
/// streams produce in that time.
constexpr uint32_t kReadReserveMs = 40;

/**
 * @brief Time between FIFO reads for the streams that are running.
 *
 * The accelerometer and the gyroscope share one FIFO, so the shorter of the
 * two requests sets the pace for both. A stream that is off does not count,
 * whatever it last asked for.
 *
 * @param accelOn        Whether the accelerometer feeds the FIFO.
 * @param accelLatencyMs What is asked for it, 0 for no request.
 * @param gyroOn         Whether the gyroscope feeds the FIFO.
 * @param gyroLatencyMs  What is asked for it, 0 for no request.
 * @return Period in ms, within [kMinFifoLatencyMs, kDefaultFifoLatencyMs].
 */
inline uint32_t periodMs(bool     accelOn,
                         uint32_t accelLatencyMs,
                         bool     gyroOn,
                         uint32_t gyroLatencyMs)
{
    uint32_t period = kDefaultFifoLatencyMs;

    if (accelOn && accelLatencyMs != 0 && accelLatencyMs < period) {
        period = accelLatencyMs;
    }

    if (gyroOn && gyroLatencyMs != 0 && gyroLatencyMs < period) {
        period = gyroLatencyMs;
    }

    if (period < kMinFifoLatencyMs) {
        period = kMinFifoLatencyMs;
    }

    return period;
}

} // namespace ImuFifoCadence

#endif // COMPONENT_IMU_FIFO_CADENCE_HPP
