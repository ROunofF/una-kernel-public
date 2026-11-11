/**
 ******************************************************************************
 * @file    TimeUtils.c
 * @brief   Time utilities
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Time/TimeUtils.h"

/**
 * @brief Convert UNIX epoch time (UTC seconds since 1970-01-01 00:00:00) to calendar date/time (UTC).
 *
 * This function converts a 64-bit UNIX epoch value to a broken-down time
 * in UTC, filling a user-provided @c struct tm.
 *
 * @param[in]  t   Epoch time in seconds (can be negative for dates before 1970).
 * @param[out] out Pointer to a user-provided @c struct tm to store the result.
 *
 * @return Pointer to the filled @c struct tm on success, or NULL if @p out is NULL.
 *
 * @note The returned @c struct tm is always in UTC. @c tm_isdst is set to 0.
 * @note Leap seconds are not accounted for (same behavior as standard C library).
 */
struct tm* TimeUtils_epoch2tm(time_t t, struct tm* out)
{
    if (!out) {
        return NULL;
    }

    int64_t z = (int64_t)t / 86400;
    int64_t r = (int64_t)t % 86400;

    if (r < 0) {
        r += 86400;
        z -= 1;
    }

    // 1970-01-01 -> shift to "civil"
    int64_t  era = (z >= -719468 ? z + 719468 : z + 719468 - 146096) / 146097;
    uint32_t doe = (uint32_t )(z - era * 146097 + 719468);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int      y   = (int)(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp  = (5 * doy + 2) / 153;
    uint32_t d   = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m   = mp + (mp < 10 ? 3 : -9);

    y += (m <= 2);

    out->tm_year  = y - 1900;
    out->tm_mon   = m - 1;
    out->tm_mday  = d;
    out->tm_wday  = (int)((z + 4) % 7);

    if (out->tm_wday < 0) {
        out->tm_wday += 7;
    }
    
    out->tm_yday  = (int) doy;
    out->tm_hour  = (int) (r / 3600);
    out->tm_min   = (int) ((r % 3600) / 60);
    out->tm_sec   = (int) (r % 60);
    out->tm_isdst = 0;

    return out;
}

/**
 * @brief Convert calendar date/time (UTC) to UNIX epoch time (UTC seconds since 1970-01-01 00:00:00).
 *
 * This function converts a broken-down UTC time in @c struct tm into a UNIX epoch value.
 *
 * @param[in] tm Pointer to a @c struct tm with fields set to UTC time.
 *
 * @return Epoch time (seconds since 1970-01-01 00:00:00 UTC).
 *
 * @note Leap seconds are not accounted for (same behavior as standard C library).
 * @note The input year range is effectively 1900–~2400 (limited by @c int32_t and @c time_t size).
 */
time_t TimeUtils_tm2epoch(const struct tm *tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;     // 1..12
    int d = tm->tm_mday;        // 1..31

    if (m <= 2) {
        y -= 1;
        m += 12;
    }

    // Julian day
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153*(m - 3) + 2)/5 + d - 1;
    uint32_t doe = yoe*365 + yoe/4 - yoe/100 + doy;
    int64_t days = era*146097 + (int64_t)doe - 719468; // 1970-01-01 is 719468
    int64_t secs = days*86400 + tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec;

    return (time_t) secs;
}
