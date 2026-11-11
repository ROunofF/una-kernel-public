/**
 ******************************************************************************
 * @file    WatchSettings.hpp
 * @brief   Watch settings configuration structure.
 ******************************************************************************
 * This file contains the definition of the "Settings" structure, which
 * represents user-configurable settings accessible from the mobile application.
 ******************************************************************************
 */

#ifndef __WATCH_SETTINGS_HPP
#define __WATCH_SETTINGS_HPP

#include <cstdint>
#include "HeartRateZones.hpp"
#include "DailyGoals.hpp"

/**
 * @brief Watch settings configuration structure.
 * @note Contains all user-configurable settings accessible from mobile app.
 */
struct WatchSettings {

    /// Settings version.
    static constexpr uint8_t kVersion = 2;

    uint32_t version = kVersion;        ///< Settings version

    /**
     * @brief   Unit system preference.
     * @note    'false' for metric units (default), 'true' for imperial units.
     */
    bool unitsImperial = false;

    /**
     * @brief Phone-related settings group.
     */
    struct {
        bool notifications = true;      ///< Enable/disable phone notifications on the device.
    } phone;

    HeartRateZones heartRateZones {};   ///< Heart rate zones configuration for fitness tracking.
    DailyGoals dailyGoals {};           ///< Daily fitness goals and activity targets.

    // Added in version 2
    uint32_t heightCm;      // User height in centimeters
    float    weightKg;      // User weight in kilograms

    /**
     * @brief Equality operator to compare two settings configurations.
     * @param other The other Settings object to compare.
     * @return 'true' if the configurations are equal, 'false' otherwise.
     *
     * Compares all settings fields to determine equality.
     */
    bool operator==(const WatchSettings &other) const
    {
        return (unitsImperial == other.unitsImperial &&
                phone.notifications == other.phone.notifications &&
                heartRateZones == other.heartRateZones &&
                dailyGoals == other.dailyGoals &&
                heightCm == other.heightCm &&
                weightKg == other.weightKg);
    }

    /**
     * @brief Inequality operator to compare two settings configurations.
     * @param other The other Settings object to compare.
     * @return 'true' if the configurations are not equal, 'false' otherwise.
     */
    bool operator!=(const WatchSettings &other) const
    {
        return !(*this == other);
    }
};

#endif // __WATCH_SETTINGS_HPP
