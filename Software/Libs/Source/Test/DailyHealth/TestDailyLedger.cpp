/**
 ******************************************************************************
 * @file    TestDailyLedger.cpp
 * @brief   Unit tests for DailyHealth::DailyLedger.
 *
 *          The ledger is pure - no OS, no FS, no sensors, no threads -
 *          so these tests need no fakes at all: they feed plain
 *          Snapshot / std::tm values and assert on the public state.
 *          They run on-device through the existing Test_execute runner.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestDLedger"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/DailyHealth/TestDailyLedger.hpp"
#include "DailyHealth/DailyLedger.hpp"

namespace {

using DailyHealth::DailyDate;
using DailyHealth::DailyLedger;
using DailyHealth::DailyMetrics;
using DailyHealth::DailyRecord;

// === Helpers =================================================================

std::tm tmAt(int year, int mon, int mday, int hour, int min)
{
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = 0;
    return t;
}

DailyDate ddAt(int year, int mon, int mday)
{
    return DailyDate::fromTm(tmAt(year, mon, mday, 0, 0));
}

DailyLedger::Snapshot snap(uint32_t steps, uint32_t fUp, uint32_t fDown,
                           uint32_t actMin, float hr)
{
    DailyLedger::Snapshot s{};
    s.steps           = steps;
    s.floorsUp        = fUp;
    s.floorsDown      = fDown;
    s.activityMinutes = actMin;
    s.avgHr           = hr;
    return s;
}

bool expectU32(uint32_t got, uint32_t want, const char* what)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got %u, want %u\n", what,
              (unsigned)got, (unsigned)want);
    return false;
}

bool expectBool(bool got, bool want, const char* what)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got %d, want %d\n", what, (int)got, (int)want);
    return false;
}

// === Scenarios ===============================================================

// Boot with no recovery file -> fresh empty day, nothing to archive.
bool testBootFresh()
{
    DailyLedger l;
    const DailyLedger::RestoreOutcome ro =
        l.bootRestore(ddAt(2025, 1, 2), /*hadRecovery*/ false,
                      DailyMetrics{}, std::tm{});

    bool ok = true;
    ok &= expectBool(ro.archiveStale, false, "bootFresh archiveStale");
    ok &= expectBool(l.date().sameDay(ddAt(2025, 1, 2)), true,
                     "bootFresh date");
    ok &= expectU32(l.metrics().steps, 0, "bootFresh steps");
    ok &= expectBool(l.dirty(), false, "bootFresh dirty");
    return ok;
}

// Boot with a recovery file for today -> metrics adopted as-is.
bool testBootRecoverToday()
{
    DailyMetrics rec{};
    rec.steps      = 1234;
    rec.floorsUp   = 3;
    rec.floorsDown = 1;

    DailyLedger l;
    const DailyLedger::RestoreOutcome ro =
        l.bootRestore(ddAt(2025, 1, 2), /*hadRecovery*/ true,
                      rec, tmAt(2025, 1, 2, 7, 30));

    bool ok = true;
    ok &= expectBool(ro.archiveStale, false, "recoverToday archiveStale");
    ok &= expectU32(l.metrics().steps, 1234, "recoverToday steps");
    ok &= expectU32(l.metrics().floorsUp, 3, "recoverToday floorsUp");
    ok &= expectU32(l.metrics().floorsDown, 1, "recoverToday floorsDown");
    ok &= expectBool(l.date().sameDay(ddAt(2025, 1, 2)), true,
                     "recoverToday date");
    ok &= expectBool(l.dirty(), false, "recoverToday dirty");
    return ok;
}

// Boot with a recovery file from an earlier day -> hand it back for
// archiving, open a fresh day for today.
bool testBootStale()
{
    DailyMetrics rec{};
    rec.steps           = 5000;
    rec.floorsUp        = 4;
    rec.floorsDown      = 2;
    rec.activityMinutes = 30;

    DailyLedger l;
    const DailyLedger::RestoreOutcome ro =
        l.bootRestore(ddAt(2025, 1, 2), /*hadRecovery*/ true,
                      rec, tmAt(2025, 1, 1, 23, 40));

    bool ok = true;
    ok &= expectBool(ro.archiveStale, true, "bootStale archiveStale");
    ok &= expectU32(l.finishedRecord().steps, 5000, "bootStale fin steps");
    ok &= expectU32(l.finishedRecord().floorsUp, 4, "bootStale fin floorsUp");
    ok &= expectU32(l.finishedRecord().floorsDown, 2, "bootStale fin floorsDown");
    ok &= expectU32(l.finishedRecord().activityMinutes, 30,
                    "bootStale fin activityMinutes");
    ok &= expectBool(DailyDate::fromTm(l.finishedTm()).sameDay(ddAt(2025, 1, 1)),
                     true, "bootStale finishedTm");
    ok &= expectBool(l.date().sameDay(ddAt(2025, 1, 2)), true,
                     "bootStale fresh date");
    ok &= expectU32(l.metrics().steps, 0, "bootStale fresh steps");
    ok &= expectBool(l.dirty(), false, "bootStale fresh dirty");
    return ok;
}

// First tick after boot: the baseline is 0, so the whole boot-to-now
// cumulative is credited to today.
bool testFirstTickCreditsBootToNow()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    const DailyLedger::TickOutcome out =
        l.onMinuteTick(tmAt(2025, 1, 2, 8, 15), snap(100, 2, 1, 5, 70.0f));

    bool ok = true;
    ok &= expectBool(out.dayRolled, false, "firstTick dayRolled");
    ok &= expectU32(l.metrics().steps, 100, "firstTick steps");
    ok &= expectU32(l.metrics().floorsUp, 2, "firstTick floorsUp");
    ok &= expectU32(l.metrics().floorsDown, 1, "firstTick floorsDown");
    ok &= expectU32(l.metrics().activityMinutes, 5, "firstTick activityMinutes");
    ok &= expectU32(l.metrics().hrPerMinute[8][15], 70, "firstTick hrSlot");
    ok &= expectBool(l.dirty(), true, "firstTick dirty");
    return ok;
}

// Subsequent ticks accumulate the inter-tick delta; a tick with no
// forward progress adds nothing.
bool testDeltaAccumulates()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    l.onMinuteTick(tmAt(2025, 1, 2, 8, 0), snap(100, 0, 0, 0, 0.0f));
    l.onMinuteTick(tmAt(2025, 1, 2, 8, 1), snap(250, 0, 0, 0, 0.0f));
    l.onMinuteTick(tmAt(2025, 1, 2, 8, 2), snap(250, 0, 0, 0, 0.0f));

    return expectU32(l.metrics().steps, 250, "deltaAccumulates steps");
}

// A tick with zero deltas must not flag the ledger dirty; the first
// real delta does.
bool testStaticSourceNotDirty()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    l.onMinuteTick(tmAt(2025, 1, 2, 8, 0), snap(0, 0, 0, 0, 0.0f));

    bool ok = true;
    ok &= expectBool(l.dirty(), false, "staticSource dirty after zero tick");

    l.onMinuteTick(tmAt(2025, 1, 2, 8, 1), snap(10, 0, 0, 0, 0.0f));
    ok &= expectBool(l.dirty(), true, "staticSource dirty after real delta");
    return ok;
}

// HR is written into its [hour][minute] slot. An HR-only minute (no
// scalar delta) DOES dirty the ledger so the fresh sample is flushed
// to the recovery file. A repeat tick that writes the same value into
// the same slot is a no-op and must not re-dirty.
bool testHrSlotAndHrOnlyDirties()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    l.onMinuteTick(tmAt(2025, 1, 2, 10, 0), snap(0, 0, 0, 0, 88.0f));

    bool ok = true;
    ok &= expectU32(l.metrics().hrPerMinute[10][0], 88, "hrSlot value");
    ok &= expectBool(l.dirty(), true, "hrOnly dirties");

    // Same slot, same value -> nothing changed -> not dirty again.
    l.clearDirty();
    l.onMinuteTick(tmAt(2025, 1, 2, 10, 0), snap(0, 0, 0, 0, 88.0f));
    ok &= expectBool(l.dirty(), false, "hrOnly unchanged not dirty");
    return ok;
}

// Crossing midnight: the crossing-minute delta is credited to the
// closing day, the ledger opens a fresh day itself, and the baseline
// is retained so day 2 is not double-counted. No data is lost across
// the boundary.
bool testDayRollover()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    // Day 1 activity.
    l.onMinuteTick(tmAt(2025, 1, 2, 12, 0), snap(500, 3, 1, 20, 80.0f));

    // Tick that crosses into day 2. The 300/2/1/5 crossing delta
    // belongs to day 1 (it elapsed before midnight).
    const DailyLedger::TickOutcome out =
        l.onMinuteTick(tmAt(2025, 1, 3, 0, 0), snap(800, 5, 2, 25, 90.0f));

    bool ok = true;
    ok &= expectBool(out.dayRolled, true, "rollover dayRolled");
    // Finished day = day 1 base + crossing delta = 800/5/2/25.
    ok &= expectU32(l.finishedRecord().steps, 800, "rollover fin steps");
    ok &= expectU32(l.finishedRecord().floorsUp, 5, "rollover fin floorsUp");
    ok &= expectU32(l.finishedRecord().floorsDown, 2, "rollover fin floorsDown");
    ok &= expectU32(l.finishedRecord().activityMinutes, 25,
                    "rollover fin activityMinutes");
    ok &= expectBool(DailyDate::fromTm(l.finishedTm()).sameDay(ddAt(2025, 1, 2)),
                     true, "rollover finishedTm");
    // The ledger already opened the fresh day; publish reflects it.
    ok &= expectU32(out.publish.steps, 0, "rollover publish steps");
    ok &= expectBool(l.date().sameDay(ddAt(2025, 1, 3)), true,
                     "rollover new date");
    ok &= expectU32(l.metrics().steps, 0, "rollover new day cleared");

    // Next tick on day 2 credits only the post-crossing delta - the
    // baseline (800/5/2/25) was retained, so no double-count.
    l.onMinuteTick(tmAt(2025, 1, 3, 0, 1), snap(900, 6, 3, 30, 95.0f));
    ok &= expectU32(l.metrics().steps, 100, "rollover day2 steps");
    ok &= expectU32(l.metrics().floorsUp, 1, "rollover day2 floorsUp");
    ok &= expectU32(l.metrics().floorsDown, 1, "rollover day2 floorsDown");
    ok &= expectU32(l.metrics().activityMinutes, 5, "rollover day2 activityMinutes");
    return ok;
}

// resetTo() empties today and re-anchors the delta baseline so the
// next tick credits a zero delta.
bool testResetTo()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));
    l.onMinuteTick(tmAt(2025, 1, 2, 8, 0), snap(100, 1, 0, 2, 60.0f));

    l.resetTo(ddAt(2025, 1, 2), snap(1000, 10, 5, 50, 0.0f));

    bool ok = true;
    ok &= expectU32(l.metrics().steps, 0, "resetTo cleared steps");
    ok &= expectBool(l.dirty(), false, "resetTo cleared dirty");

    // Baseline is now (1000/10/5/50): an identical reading adds nothing.
    l.onMinuteTick(tmAt(2025, 1, 2, 9, 0), snap(1000, 10, 5, 50, 0.0f));
    ok &= expectU32(l.metrics().steps, 0, "resetTo zero-delta steps");

    // Forward progress from the new baseline is credited normally.
    l.onMinuteTick(tmAt(2025, 1, 2, 9, 1), snap(1100, 12, 6, 52, 0.0f));
    ok &= expectU32(l.metrics().steps, 100, "resetTo post steps");
    ok &= expectU32(l.metrics().floorsUp, 2, "resetTo post floorsUp");
    ok &= expectU32(l.metrics().floorsDown, 1, "resetTo post floorsDown");
    ok &= expectU32(l.metrics().activityMinutes, 2, "resetTo post activityMinutes");
    return ok;
}

// uint16 accumulators saturate instead of wrapping.
bool testSaturationU16()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));

    l.onMinuteTick(tmAt(2025, 1, 2, 8, 0), snap(0, 70000, 0, 70000, 0.0f));

    bool ok = true;
    ok &= expectU32(l.metrics().floorsUp, 65535, "saturation floorsUp");
    ok &= expectU32(l.metrics().activityMinutes, 65535,
                    "saturation activityMinutes");
    return ok;
}

// composeRecord() passes the accumulated scalars straight through.
bool testComposeRecord()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));
    l.onMinuteTick(tmAt(2025, 1, 2, 10, 0), snap(300, 4, 2, 15, 100.0f));

    const DailyRecord r = l.composeRecord(tmAt(2025, 1, 2, 10, 1));

    bool ok = true;
    ok &= expectU32(r.steps, 300, "compose steps");
    ok &= expectU32(r.floorsUp, 4, "compose floorsUp");
    ok &= expectU32(r.floorsDown, 2, "compose floorsDown");
    ok &= expectU32(r.activityMinutes, 15, "compose activityMinutes");
    return ok;
}

// composeRecord() averages HR only up to the supplied `now`. A sample
// recorded later in the day must not count until `now` reaches it -
// regression test for the endDay-from-day-open-time bug.
bool testComposeRecordWindow()
{
    DailyLedger l;
    l.startDay(ddAt(2025, 1, 2));
    l.onMinuteTick(tmAt(2025, 1, 2, 10, 0), snap(0, 0, 0, 0, 120.0f));
    l.onMinuteTick(tmAt(2025, 1, 2, 14, 0), snap(0, 0, 0, 0, 80.0f));

    bool ok = true;
    // now = 10:30 -> only the 10:00 sample is inside the window.
    ok &= expectU32(l.composeRecord(tmAt(2025, 1, 2, 10, 30)).averageHeartRate,
                    120, "composeWindow early AHR");
    // now = 14:30 -> both samples count: mean (120 + 80) / 2.
    ok &= expectU32(l.composeRecord(tmAt(2025, 1, 2, 14, 30)).averageHeartRate,
                    100, "composeWindow late AHR");
    return ok;
}

} // namespace

void TestDailyHealth_DailyLedger_execute()
{
    LOG_INFO("DailyLedger tests start\n");

    if (!testBootFresh())                { LOG_ERROR("FAIL\n"); return; }
    if (!testBootRecoverToday())         { LOG_ERROR("FAIL\n"); return; }
    if (!testBootStale())                { LOG_ERROR("FAIL\n"); return; }
    if (!testFirstTickCreditsBootToNow()){ LOG_ERROR("FAIL\n"); return; }
    if (!testDeltaAccumulates())         { LOG_ERROR("FAIL\n"); return; }
    if (!testStaticSourceNotDirty())     { LOG_ERROR("FAIL\n"); return; }
    if (!testHrSlotAndHrOnlyDirties())   { LOG_ERROR("FAIL\n"); return; }
    if (!testDayRollover())              { LOG_ERROR("FAIL\n"); return; }
    if (!testResetTo())                  { LOG_ERROR("FAIL\n"); return; }
    if (!testSaturationU16())            { LOG_ERROR("FAIL\n"); return; }
    if (!testComposeRecord())            { LOG_ERROR("FAIL\n"); return; }
    if (!testComposeRecordWindow())      { LOG_ERROR("FAIL\n"); return; }

    LOG_INFO("DailyLedger tests PASS\n");
}
