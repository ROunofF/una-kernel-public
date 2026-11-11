/**
 ******************************************************************************
 * @file    TestHealthCalculator.cpp
 * @brief   Unit tests for DailyHealth::HealthCalculator.
 *
 *          The calculator is a pure function - these tests need no FS,
 *          no FreeRTOS, no sensors. They run on-device through the
 *          existing test runner (Test_execute).
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestHC"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/DailyHealth/TestHealthCalculator.hpp"
#include "DailyHealth/HealthCalculator.hpp"

#include <cstring>

namespace {

using DailyHealth::DailyMetrics;
using DailyHealth::DailyRecord;
using DailyHealth::HealthCalculator;

// Build a tm representing midnight on 2025-01-02. Used as a stable
// "date" anchor; only year/month/day fields matter for calculator
// (hours/minutes are only used when upToNow=true).
std::tm midnight()
{
    std::tm t{};
    t.tm_year = 125;  // 2025
    t.tm_mon  = 0;    // January
    t.tm_mday = 2;
    t.tm_hour = 0;
    t.tm_min  = 0;
    t.tm_sec  = 0;
    return t;
}

// Linear minute index in [0..1439] from hour/minute.
constexpr uint32_t slot(uint32_t hour, uint32_t min)
{
    return hour * 60u + min;
}

bool checkRecord(const DailyRecord& got,
                 const DailyRecord& want,
                 const char*        scenario)
{
    if (got.steps            == want.steps            &&
        got.floorsUp         == want.floorsUp         &&
        got.floorsDown       == want.floorsDown       &&
        got.activityMinutes  == want.activityMinutes  &&
        got.averageHeartRate == want.averageHeartRate &&
        got.restingHeartRate == want.restingHeartRate) {
        return true;
    }

    LOG_ERROR("%s mismatch:\n", scenario);
    LOG_INFO_WP("  steps:       got %u, want %u\n",
                (unsigned)got.steps,            (unsigned)want.steps);
    LOG_INFO_WP("  floorsUp:    got %u, want %u\n",
                (unsigned)got.floorsUp,         (unsigned)want.floorsUp);
    LOG_INFO_WP("  floorsDown:  got %u, want %u\n",
                (unsigned)got.floorsDown,       (unsigned)want.floorsDown);
    LOG_INFO_WP("  activityMin: got %u, want %u\n",
                (unsigned)got.activityMinutes,  (unsigned)want.activityMinutes);
    LOG_INFO_WP("  avgHR:       got %u, want %u\n",
                (unsigned)got.averageHeartRate, (unsigned)want.averageHeartRate);
    LOG_INFO_WP("  restingHR:   got %u, want %u\n",
                (unsigned)got.restingHeartRate, (unsigned)want.restingHeartRate);
    return false;
}

// === Scenarios ===============================================================

// Empty metrics -> all zeros, including HR fields.
bool testEmpty()
{
    DailyMetrics m{};
    const DailyRecord got =
        HealthCalculator::compute(m, midnight(), /*upToNow*/ false);

    DailyRecord want{};
    return checkRecord(got, want, "empty");
}

// A 30-slot timeline 23:30..23:59, HR samples 60x10, 95x10, 70x10,
// computed with upToNow=false (whole day).
//
//   AHR: (10*60 + 10*95 + 10*70) / 30 = 75
//   RHR: only one full 30-min window covers the data; its mean is also
//        75, hence RHR = 75.
bool testEmu30min()
{
    DailyMetrics m{};

    // 23:30 .. 23:39 = 60
    for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t s = slot(23, 30 + i);
        (&m.hrPerMinute[0][0])[s] = 60;
    }
    // 23:40 .. 23:49 = 95
    for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t s = slot(23, 40 + i);
        (&m.hrPerMinute[0][0])[s] = 95;
    }
    // 23:50 .. 23:59 = 70
    for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t s = slot(23, 50 + i);
        (&m.hrPerMinute[0][0])[s] = 70;
    }

    m.steps           = 200;
    m.floorsUp        = 1;
    m.floorsDown      = 2;
    m.activityMinutes = 10;

    const DailyRecord got =
        HealthCalculator::compute(m, midnight(), /*upToNow*/ false);

    DailyRecord want{};
    want.steps            = 200;
    want.floorsUp         = 1;
    want.floorsDown       = 2;
    want.activityMinutes  = 10;
    want.averageHeartRate = 75;
    want.restingHeartRate = 75;
    return checkRecord(got, want, "kEmu30min");
}

// Single full 30-minute window of low HR followed by another full window
// of high HR. RHR should pick the lower window.
bool testRhrPicksLowest()
{
    DailyMetrics m{};

    // 00:00..00:29 = 55 -> window mean = 55
    for (uint32_t i = 0; i < 30; ++i) {
        (&m.hrPerMinute[0][0])[slot(0, i)] = 55;
    }
    // 00:30..00:59 = 90 -> window mean = 90
    for (uint32_t i = 0; i < 30; ++i) {
        (&m.hrPerMinute[0][0])[slot(0, 30 + i)] = 90;
    }

    const DailyRecord got =
        HealthCalculator::compute(m, midnight(), /*upToNow*/ false);

    if (got.restingHeartRate != 55) {
        LOG_ERROR("rhrPicksLowest: got RHR %u, want 55\n",
                  (unsigned)got.restingHeartRate);
        return false;
    }
    // (55*30 + 90*30) / 60 = 72
    if (got.averageHeartRate != 72) {
        LOG_ERROR("rhrPicksLowest: got AHR %u, want 72\n",
                  (unsigned)got.averageHeartRate);
        return false;
    }
    return true;
}

// upToNow=true must clip the iteration at the current minute. Samples
// after the cutoff must not influence AHR or RHR.
bool testUpToNowClips()
{
    DailyMetrics m{};

    // First full window 00:00..00:29 = 55 (would set RHR=55 if seen)
    for (uint32_t i = 0; i < 30; ++i) {
        (&m.hrPerMinute[0][0])[slot(0, i)] = 55;
    }
    // Second full window 00:30..00:59 = 200 (out-of-range high; if
    // upToNow=true with date=00:29, this window is past the cutoff
    // and must be ignored).
    for (uint32_t i = 0; i < 30; ++i) {
        (&m.hrPerMinute[0][0])[slot(0, 30 + i)] = 200;
    }

    std::tm date = midnight();
    date.tm_hour = 0;
    date.tm_min  = 29;  // endDay = 29 -> loop covers [0..29]

    const DailyRecord got =
        HealthCalculator::compute(m, date, /*upToNow*/ true);

    // Loop body increments cnt30 for i in [0..29] -> cnt30 reaches 30,
    // window mean = 55, RHR = 55. Second window never runs.
    if (got.restingHeartRate != 55) {
        LOG_ERROR("upToNowClips: got RHR %u, want 55\n",
                  (unsigned)got.restingHeartRate);
        return false;
    }
    if (got.averageHeartRate != 55) {
        LOG_ERROR("upToNowClips: got AHR %u, want 55\n",
                  (unsigned)got.averageHeartRate);
        return false;
    }
    return true;
}

// Partial (non-30-aligned) data must not produce a stray RHR: a
// tail-window without 30 entries never triggers the `cnt30 % 30 == 0`
// branch, so it cannot lower RHR.
bool testPartialWindowIgnored()
{
    DailyMetrics m{};

    // 5 samples only, at the start of the day.
    for (uint32_t i = 0; i < 5; ++i) {
        (&m.hrPerMinute[0][0])[slot(0, i)] = 50;
    }

    const DailyRecord got =
        HealthCalculator::compute(m, midnight(), /*upToNow*/ false);

    // AHR is sample-counted (not window-counted) so it sees the 5
    // values: AHR = 50.
    if (got.averageHeartRate != 50) {
        LOG_ERROR("partialWindowIgnored: got AHR %u, want 50\n",
                  (unsigned)got.averageHeartRate);
        return false;
    }
    // The 5 samples are inside the very first 30-minute window. That
    // window's mean is 50 (only the non-zero samples count toward
    // sum/cnt). So RHR ends up 50 - same value, same window.
    if (got.restingHeartRate != 50) {
        LOG_ERROR("partialWindowIgnored: got RHR %u, want 50\n",
                  (unsigned)got.restingHeartRate);
        return false;
    }
    return true;
}

} // namespace

void TestDailyHealth_HealthCalculator_execute()
{
    LOG_INFO("HealthCalculator tests start\n");

    if (!testEmpty())               { LOG_ERROR("FAIL\n"); return; }
    if (!testEmu30min())            { LOG_ERROR("FAIL\n"); return; }
    if (!testRhrPicksLowest())      { LOG_ERROR("FAIL\n"); return; }
    if (!testUpToNowClips())        { LOG_ERROR("FAIL\n"); return; }
    if (!testPartialWindowIgnored()) { LOG_ERROR("FAIL\n"); return; }

    LOG_INFO("HealthCalculator tests PASS\n");
}
