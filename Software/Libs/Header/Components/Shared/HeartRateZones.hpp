/**
 ******************************************************************************
 * @file    HeartRateZones.hpp
 * @brief   Heart rate zones configuration structure.
 ******************************************************************************
 * This file contains the definition of the "HeartRateZones" structure, which
 * represents heart rate zones thresholds for fitness tracking and training.
 ******************************************************************************
 */

#ifndef __HEART_RATE_ZONES_HPP
#define __HEART_RATE_ZONES_HPP

#include <cstdint>

/**
 * @brief Heart rate zones configuration structure.
 * @note Defines threshold values for different heart rate training zones.
 */
struct HeartRateZones {

    /// Maximum number of zones.
    static constexpr uint8_t kMaxNum = 7;

    /// Maximum number of threshold.
    static constexpr uint8_t kMaxThreshold = kMaxNum - 1;

    /**
     * @brief   Heart rate thresholds array in beats per minute (BPM).
     * @note    Array contains (kMaxNum - 1) threshold values that define zone boundaries.
     *          Zone 1: < thresholds[0], Zone 2: thresholds[0] to thresholds[1], etc.
     *          Default values: 95, 114, 133, 152, 171, 190 BPM.
     */
    uint8_t thresholds[kMaxThreshold] { 95, 114, 133, 152, 171, 190 };

    /**
     * @brief Equality operator to compare two heart rate zones configurations.
     * @param other The other HeartRateZones object to compare.
     * @return 'true' if the configurations are equal, 'false' otherwise.
     *
     * Compares all threshold values to determine equality.
     */
    bool operator==(const HeartRateZones &other) const
    {
        for (uint8_t i = 0; i < kMaxThreshold; i++) {
            if (thresholds[i] != other.thresholds[i]) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Inequality operator to compare two heart rate zones configurations.
     * @param other The other HeartRateZones object to compare.
     * @return 'true' if the configurations are not equal, 'false' otherwise.
     */
    bool operator!=(const HeartRateZones &other) const
    {
        return !(*this == other);
    }
};

#endif // __HEART_RATE_ZONES_HPP
