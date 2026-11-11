/**
 ******************************************************************************
 * @file    Types.hpp
 * @brief   Core domain types of the Daily Health subsystem.
 *
 *          Pure data types: no I/O, no FreeRTOS, no FS dependencies.
 *          Safe to include from anywhere, including unit tests.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_TYPES_HPP
#define COMPONENT_DAILY_HEALTH_TYPES_HPP

#include <cstdint>
#include <ctime>

namespace DailyHealth
{

/**
 * @brief Compact, layout-stable date+time-of-day used for serialised
 *        records and recovery files.
 *
 * Why not std::tm? std::tm carries platform-dependent extras
 * (tm_isdst, possibly tm_zone/tm_gmtoff pointers on some libc's),
 * which makes binary-serialising it brittle. DailyDate has a fixed
 * 8-byte layout under the only alignment we care about and never
 * surprises the storage layer.
 */
struct DailyDate
{
    uint16_t year;     ///< Full year, e.g. 2025.
    uint8_t  mon;      ///< Calendar month, 1..12.
    uint8_t  mday;     ///< Day of month, 1..31.
    uint8_t  hour;     ///< 0..23.
    uint8_t  min;      ///< 0..59.
    uint8_t  sec;      ///< 0..59.
    uint8_t  _pad;     ///< Pad to 8 bytes; must be zero on the wire.

    static DailyDate fromTm(const std::tm& t)
    {
        DailyDate d{};
        d.year = static_cast<uint16_t>(t.tm_year + 1900);
        d.mon  = static_cast<uint8_t>(t.tm_mon + 1);
        d.mday = static_cast<uint8_t>(t.tm_mday);
        d.hour = static_cast<uint8_t>(t.tm_hour);
        d.min  = static_cast<uint8_t>(t.tm_min);
        d.sec  = static_cast<uint8_t>(t.tm_sec);
        d._pad = 0;
        return d;
    }

    std::tm toTm() const
    {
        std::tm t{};
        t.tm_year = static_cast<int>(year) - 1900;
        t.tm_mon  = static_cast<int>(mon) - 1;
        t.tm_mday = static_cast<int>(mday);
        t.tm_hour = static_cast<int>(hour);
        t.tm_min  = static_cast<int>(min);
        t.tm_sec  = static_cast<int>(sec);
        // tm_wday / tm_yday / tm_isdst left zero; callers that need them
        // must run mktime() to normalise.
        return t;
    }

    bool sameDay(const DailyDate& o) const
    {
        return year == o.year && mon == o.mon && mday == o.mday;
    }
};

/**
 * @brief Per-minute metrics accumulated through the current day.
 *
 * Held in RAM by the Service and periodically persisted by the storage
 * layer. The daily JSON file is computed from a finalised instance of
 * this struct via @ref HealthCalculator.
 *
 * The HR matrix stores one byte per minute (24*60 = 1440 entries).
 * A future revision will likely add a parallel time-stamped HR series
 * for the BLE app's day-graph view; that will live in a separate file
 * and a separate type - do not fold it into this struct.
 */
struct DailyMetrics
{
    uint32_t steps;            ///< Total steps for the day.
    uint16_t floorsUp;         ///< Floors climbed.
    uint16_t floorsDown;       ///< Floors descended.
    uint16_t activityMinutes;  ///< Minutes the user was active.

    /// HR per minute, hour-major. 0 means "no valid sample for this slot".
    uint8_t  hrPerMinute[24][60];
};

/**
 * @brief Daily summary record, the canonical "what happened this day".
 *
 * Computed on demand for the current day; persisted as JSON for past
 * days. This is the type the BLE consumer ultimately receives.
 */
struct DailyRecord
{
    uint32_t steps;
    uint16_t floorsUp;
    uint16_t floorsDown;
    uint16_t activityMinutes;
    uint8_t  restingHeartRate;
    uint8_t  averageHeartRate;
};

/**
 * @brief Per-minute heart-rate grid for a single day.
 *
 * Layout matches @ref DailyMetrics::hrPerMinute (hour-major, 0 = no
 * valid sample). This is the transport type used when the matrix
 * crosses an API boundary - storage save/load and the BLE day-graph
 * getter. The internal RAM representation in @ref DailyMetrics keeps
 * its raw 2D array so existing serialisers and HealthCalculator stay
 * untouched.
 */
struct DailyHrMatrix
{
    uint8_t cells[24][60];
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_TYPES_HPP
