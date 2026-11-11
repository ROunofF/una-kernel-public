/**
 ******************************************************************************
 * @file    FifoWatermark.hpp
 * @brief   BMI270 FIFO watermark for a wanted read period.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_BMI270_FIFO_WATERMARK_HPP
#define HARDWARE_BMI270_FIFO_WATERMARK_HPP

#include <cstdint>

namespace Hardware::Bmi270Fifo
{

/**
 * @brief FIFO fill level, in bytes, that one read period produces.
 *
 * In header mode a reading is one frame: a header byte and six data bytes.
 * When both sensors run, readings that fall on the same instant share a
 * frame of thirteen bytes and the faster sensor fills the rest with frames of
 * its own. Without headers the two are always packed together.
 *
 * The level is what the frames of one period add up to, and never less than
 * one frame: a level of zero never raises the interrupt, so a period shorter
 * than the data period would stop the FIFO from being read at all. A level cut
 * to the limit need not fall on a frame boundary; the FIFO fills a frame at a
 * time, so the interrupt comes at the first boundary at or past it.
 *
 * The streams are given by their data periods, not their rates: the slow
 * rates of the part are fractions of a Hz, and a rate in whole Hz would count
 * their readings wrong, down to none at all.
 *
 * @param accelPeriodUs Accelerometer data period, 0 when it is off.
 * @param gyroPeriodUs  Gyroscope data period, 0 when it is off.
 * @param periodMs      Wanted time between FIFO reads.
 * @param headerEnabled Whether the FIFO runs in header mode.
 * @param limitBytes    Highest level the part raises the interrupt for.
 * @return Watermark in bytes, 0 when both sensors are off.
 */
inline uint16_t watermarkBytes(uint32_t accelPeriodUs,
                               uint32_t gyroPeriodUs,
                               uint32_t periodMs,
                               bool     headerEnabled,
                               uint16_t limitBytes)
{
    constexpr uint64_t kDataSize   = 6;
    constexpr uint64_t kHeaderSize = 1;

    const bool accelEnabled = (accelPeriodUs != 0);
    const bool gyroEnabled  = (gyroPeriodUs != 0);

    if (!accelEnabled && !gyroEnabled) {
        return 0;
    }

    const uint64_t spanUs       = static_cast<uint64_t>(periodMs) * 1000;
    const uint64_t accelSamples = accelEnabled ? spanUs / accelPeriodUs : 0;
    const uint64_t gyroSamples  = gyroEnabled ? spanUs / gyroPeriodUs : 0;
    const uint64_t fewer        = (accelSamples < gyroSamples) ? accelSamples : gyroSamples;
    const uint64_t more         = (accelSamples < gyroSamples) ? gyroSamples : accelSamples;

    uint64_t totalBytes = 0;
    uint64_t frameSize  = 0;

    if (headerEnabled) {
        if (accelEnabled && gyroEnabled) {
            frameSize  = kHeaderSize + 2 * kDataSize;
            totalBytes = fewer * frameSize + (more - fewer) * (kHeaderSize + kDataSize);
        } else {
            frameSize  = kHeaderSize + kDataSize;
            totalBytes = more * frameSize;
        }
    } else {
        frameSize  = (accelEnabled && gyroEnabled) ? 2 * kDataSize : kDataSize;
        totalBytes = more * frameSize;
    }

    if (totalBytes < frameSize) {
        totalBytes = frameSize;
    }

    if (totalBytes > limitBytes) {
        totalBytes = limitBytes;
    }

    return static_cast<uint16_t>(totalBytes);
}

/**
 * @brief Highest watermark that leaves the FIFO room for a read that comes
 *        late.
 *
 * The watermark only asks for a read. Until the read comes the FIFO goes on
 * filling, and when it is full the part drops frames. So the level is held
 * below the part's limit by what the streams produce in the time a read may
 * be late by, and never by less than one frame.
 *
 * @param accelPeriodUs Accelerometer data period, 0 when it is off.
 * @param gyroPeriodUs  Gyroscope data period, 0 when it is off.
 * @param reserveMs     How late a read may come, 0 to keep no room.
 * @param headerEnabled Whether the FIFO runs in header mode.
 * @param limitBytes    Highest level the part raises the interrupt for.
 * @return The limit to give watermarkBytes().
 */
inline uint16_t limitWithReserve(uint32_t accelPeriodUs,
                                 uint32_t gyroPeriodUs,
                                 uint32_t reserveMs,
                                 bool     headerEnabled,
                                 uint16_t limitBytes)
{
    if (reserveMs == 0) {
        return limitBytes;
    }

    const uint16_t reserve = watermarkBytes(accelPeriodUs, gyroPeriodUs, reserveMs, headerEnabled, limitBytes);

    if (reserve < limitBytes) {
        return static_cast<uint16_t>(limitBytes - reserve);
    }

    // The reserve takes the whole FIFO: a level of one frame, the lowest
    // that still raises the interrupt.
    const uint16_t frame = watermarkBytes(accelPeriodUs, gyroPeriodUs, 0, headerEnabled, limitBytes);

    return (frame != 0) ? frame : limitBytes;
}

} // namespace Hardware::Bmi270Fifo

#endif // HARDWARE_BMI270_FIFO_WATERMARK_HPP
