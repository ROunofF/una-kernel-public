/**
 ******************************************************************************
 * @file    Bmi270Odr.hpp
 * @brief   BMI270 output data rates: the period asked for, the rate the part
 *          is set to for it, and that rate's exact period.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_BMI270_ODR_HPP
#define HARDWARE_BMI270_ODR_HPP

#include <cstdint>

namespace Hardware::Bmi270Odr
{

/// Register values of the rates. kSlowest is 25/32 Hz, kFastest 1600 Hz, and
/// each value in between doubles the rate before it. The gyroscope starts at
/// 25 Hz.
constexpr uint8_t kSlowest     = 0x01;
constexpr uint8_t kGyroSlowest = 0x06;
constexpr uint8_t kFastest     = 0x0C;

/**
 * @brief Exact period of a rate, in us.
 *
 * A rate is asked for and kept as a period because the five slowest rates
 * are not whole numbers of Hz: 12.5 Hz is 80000 us, 0.78125 Hz is 1280000 us,
 * and a rate in whole Hz stands for neither.
 *
 * @param odr Register value.
 * @return Period in us, 0 for a value that is not a rate.
 */
inline uint32_t periodUs(uint8_t odr)
{
    if (odr < kSlowest || odr > kFastest) {
        return 0;
    }

    return 625u << (kFastest - odr);
}

/**
 * @brief Period of a rate in counts of the part's own counter.
 *
 * One count is 39.0625 us, and every rate's period is a power of two of
 * them: 16 at 1600 Hz, 32768 at 0.78125 Hz. The part takes a reading when
 * its counter reaches a multiple of the period.
 *
 * @param odr Register value.
 * @return Period in counts, 0 for a value that is not a rate.
 */
inline uint32_t periodCounts(uint8_t odr)
{
    if (odr < kSlowest || odr > kFastest) {
        return 0;
    }

    return 16u << (kFastest - odr);
}

/**
 * @brief The rate the part is set to for a data period asked for.
 *
 * The slowest rate whose period is no longer than the one asked for, so the
 * part never gives readings less often than asked. A period shorter than the
 * fastest rate gets the fastest rate; one longer than the slowest, the
 * slowest.
 *
 * @param wantedUs Period asked for, in us.
 * @param slowest  Slowest rate the sensor has: kSlowest or kGyroSlowest.
 * @return Register value, slowest..kFastest.
 */
inline uint8_t fromPeriodUs(uint32_t wantedUs, uint8_t slowest = kSlowest)
{
    uint8_t odr = slowest;

    while (odr < kFastest && periodUs(odr) > wantedUs) {
        ++odr;
    }

    return odr;
}

/**
 * @brief The accelerometer rate for a period asked for, with the features' say.
 *
 * The features of the part run on the accelerometer data and need it no
 * slower than a given period. While any of them is on, a slower rate asked
 * for is raised to that; a faster one is not slowed down.
 *
 * @param wantedUs     Period asked for the accelerometer's own data.
 * @param featuresOn   Whether any feature is enabled.
 * @param featureUs    Longest period the features work with.
 * @return Register value.
 */
inline uint8_t accelFromPeriodUs(uint32_t wantedUs, bool featuresOn, uint32_t featureUs)
{
    const uint8_t odr = fromPeriodUs(wantedUs);

    if (featuresOn && periodUs(odr) > featureUs) {
        return fromPeriodUs(featureUs);
    }

    return odr;
}

/**
 * @brief Whether the part can run the accelerometer at a rate with a filter
 *        mode and an averaging.
 *
 * With the filter power-optimised the part sleeps between readings and
 * averages 2^averaging samples for each, and has the time for that only up
 * to a rate that halves with every doubling of the samples: 800 Hz for one,
 * 200 Hz for four, 6.25 Hz for 128. With the filter performance-optimised it
 * runs all the time, and has no rate under 12.5 Hz. These are the limits
 * the part itself holds to.
 *
 * The part takes settings it cannot run all the same and only then reports
 * them, so they are told apart here, before they are written.
 *
 * @param odr         Register value of the rate.
 * @param performance Whether the filter is performance-optimised.
 * @param averaging   Register value of acc_bwp, 0..7.
 */
inline bool accelSettingsValid(uint8_t odr, bool performance, uint8_t averaging)
{
    if (odr < kSlowest || odr > kFastest) {
        return false;
    }

    constexpr uint8_t kPerformanceSlowest = 0x05;   // 12.5 Hz

    return performance ? (odr >= kPerformanceSlowest) : (odr + averaging < kFastest);
}

/**
 * @brief Whether the part can run the gyroscope at a rate with a filter
 *        mode.
 *
 * Power-optimised, the gyroscope has 25, 50 and 100 Hz and no more;
 * performance-optimised, all its rates.
 *
 * @param odr         Register value of the rate.
 * @param performance Whether the filter is performance-optimised.
 */
inline bool gyroSettingsValid(uint8_t odr, bool performance)
{
    if (odr < kGyroSlowest || odr > kFastest) {
        return false;
    }

    constexpr uint8_t kPowerFastest = 0x08;         // 100 Hz

    return performance || (odr <= kPowerFastest);
}

} // namespace Hardware::Bmi270Odr

#endif // HARDWARE_BMI270_ODR_HPP
