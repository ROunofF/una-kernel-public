/**
 ******************************************************************************
 * @file    HealthCalculator.hpp
 * @brief   Pure computation: DailyMetrics -> DailyRecord.
 *
 *          No I/O, no FreeRTOS, no statics-with-state. Trivial to unit
 *          test.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_HEALTH_CALCULATOR_HPP
#define COMPONENT_DAILY_HEALTH_HEALTH_CALCULATOR_HPP

#include <ctime>

#include "DailyHealth/Types.hpp"

namespace DailyHealth
{

class HealthCalculator
{
public:
    /**
     * @brief Compute the daily summary from accumulated metrics.
     *
     * @param m         Per-day accumulated metrics. Must not be mutated.
     * @param date      The date the metrics belong to. Used together
     *                  with @p upToNow to bound the HR averaging window.
     * @param upToNow   When true, HR averaging stops at the current
     *                  hour/minute of @p date; otherwise the whole day
     *                  is considered. Set true for "today" queries.
     * @return  Filled DailyRecord. Fields that cannot be computed
     *          (e.g. RHR with no valid samples) are zero.
     */
    static DailyRecord compute(const DailyMetrics& m,
                               std::tm             date,
                               bool                upToNow);
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_HEALTH_CALCULATOR_HPP
