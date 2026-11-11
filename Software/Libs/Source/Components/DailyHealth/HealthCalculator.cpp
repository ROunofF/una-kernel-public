/**
 ******************************************************************************
 * @file    HealthCalculator.cpp
 * @brief   Pure computation: DailyMetrics -> DailyRecord.
 *
 *          Pure: takes accumulated DailyMetrics + the date and returns a
 *          DailyRecord. No side effects - surfacing the resulting
 *          AHR/RHR onto the data provider is the orchestrator's job.
 ******************************************************************************
 */

#include "DailyHealth/HealthCalculator.hpp"

#include <cstdint>

namespace DailyHealth
{

DailyRecord HealthCalculator::compute(const DailyMetrics& m,
                                      std::tm             date,
                                      bool                upToNow)
{
    uint32_t endDay;
    if (upToNow) {
        endDay = static_cast<uint32_t>(date.tm_hour) * 60u
               + static_cast<uint32_t>(date.tm_min);
    } else {
        endDay = (24u * 60u) - 1u;
    }

    const uint8_t* pHr = &m.hrPerMinute[0][0];

    // Average HR over the day (zero slots are skipped).
    uint32_t sum = 0;
    uint32_t cnt = 0;
    for (uint32_t i = 0; i <= endDay; ++i) {
        if (pHr[i] != 0) {
            sum += pHr[i];
            ++cnt;
        }
    }
    const uint8_t ahr = (cnt > 0) ? static_cast<uint8_t>(sum / cnt) : 0;

    // Resting HR: the lowest non-zero 30-minute mean across the day.
    uint8_t  rhr   = 0;
    uint32_t cnt30 = 0;
    sum = 0;
    cnt = 0;
    for (uint32_t i = 0; i <= endDay; ++i) {
        if (pHr[i] != 0) {
            sum += pHr[i];
            ++cnt;
        }

        ++cnt30;
        if (cnt30 % 30u == 0u) {
            const uint8_t avg30min =
                (cnt > 0) ? static_cast<uint8_t>(sum / cnt) : 0;
            sum = 0;
            cnt = 0;

            if ((rhr == 0 || rhr > avg30min) && avg30min > 0) {
                rhr = avg30min;
            }
        }
    }

    DailyRecord r{};
    r.steps            = m.steps;
    r.floorsUp         = m.floorsUp;
    r.floorsDown       = m.floorsDown;
    r.activityMinutes  = m.activityMinutes;
    r.averageHeartRate = ahr;
    r.restingHeartRate = rhr;
    return r;
}

} // namespace DailyHealth
