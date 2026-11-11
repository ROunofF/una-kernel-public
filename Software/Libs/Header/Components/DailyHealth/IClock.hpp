/**
 ******************************************************************************
 * @file    IClock.hpp
 * @brief   Time-source abstraction for the Daily Health subsystem.
 *
 *          Exists so unit tests can drive Service with deterministic,
 *          fast-forwarding clocks.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_ICLOCK_HPP
#define COMPONENT_DAILY_HEALTH_ICLOCK_HPP

#include <ctime>

namespace DailyHealth
{

class IClock
{
public:
    virtual ~IClock() = default;

    /**
     * @brief Local-time view of the current wall clock.
     *
     * Used for day-rollover detection and for HR-slot indexing
     * (hour, minute). Implementations must be thread-safe.
     */
    virtual std::tm now() const = 0;

    /**
     * @brief Whether now() is a real local date.
     *
     * False until both the time and the time zone are set after the RTC
     * loses power - a PMIC power cycle restarts it at 2000-01-01 with no
     * zone, and only the phone supplies the zone. Once true it stays
     * true, and the time is written before its flag, so read isValid()
     * before now().
     */
    virtual bool isValid() const = 0;
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_ICLOCK_HPP
