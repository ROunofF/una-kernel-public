/**
 ******************************************************************************
 * @file    DailyGoals.hpp
 * @brief   Daily fitness goals configuration structure.
 ******************************************************************************
 * This file contains the definition of the "DailyGoals" structure, which
 * represents user-defined daily fitness and activity targets.
 ******************************************************************************
 */

#ifndef __DAILY_GOALS_HPP
#define __DAILY_GOALS_HPP

#include <cstdint>

/**
 * @brief Daily fitness goals configuration structure.
 * @note Contains user-defined targets for daily fitness activities.
 */
struct DailyGoals {

    uint32_t activityMinutes = 30;   ///< Target number of active minutes per day.
    uint32_t steps           = 5000; ///< Target number of steps per day.
    uint32_t floors          = 10;   ///< Target number of floors climbed per day.

    /**
     * @brief Equality operator to compare two daily goals configurations.
     * @param other The other DailyGoals object to compare.
     * @return 'true' if the configurations are equal, 'false' otherwise.
     *
     * Compares all goal values to determine equality.
     */
    bool operator==(const DailyGoals &other) const
    {
        return (activityMinutes == other.activityMinutes &&
                steps == other.steps &&
                floors == other.floors);
    }

    /**
     * @brief Inequality operator to compare two daily goals configurations.
     * @param other The other DailyGoals object to compare.
     * @return 'true' if the configurations are not equal, 'false' otherwise.
     */
    bool operator!=(const DailyGoals &other) const
    {
        return !(*this == other);
    }
};

#endif // __DAILY_GOALS_HPP
