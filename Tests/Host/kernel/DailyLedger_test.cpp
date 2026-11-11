/**
 * @file DailyLedger_test.cpp
 * @brief The running day survives a reboot that lost the clock.
 *
 * A 16 s R1 press makes the PMIC power-cycle the watch, which also resets the
 * RTC to 2000-01-01 until the phone or GPS sets it. The ledger must not date
 * the restored day against that clock: it holds the day unconfirmed, keeps
 * the counts gathered meanwhile out of every day, and settles both once the
 * clock is valid.
 */

#include <gtest/gtest.h>

#include "DailyHealth/DailyLedger.hpp"

#include <cstdint>
#include <ctime>

using DailyHealth::DailyDate;
using DailyHealth::DailyLedger;
using DailyHealth::DailyMetrics;
using DailyHealth::DailyRecord;

namespace {

std::tm tmAt(int year, int mon, int mday, int hour, int min)
{
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min  = min;
    return t;
}

DailyDate ddAt(int year, int mon, int mday, int hour = 0, int min = 0)
{
    return DailyDate::fromTm(tmAt(year, mon, mday, hour, min));
}

DailyLedger::Snapshot snap(uint32_t steps, float hr = 0.0f)
{
    DailyLedger::Snapshot s{};
    s.steps = steps;
    s.avgHr = hr;
    return s;
}

// The RTC's power-on value after the backup domain lost power.
const std::tm kUnsetNow = tmAt(2000, 1, 1, 0, 3);

// A day restored from dh.tmp: 5000 steps and one HR minute at 09:00.
DailyMetrics restored()
{
    DailyMetrics m{};
    m.steps             = 5000;
    m.hrPerMinute[9][0] = 70;
    return m;
}

/// Boots a ledger the way Service does after a power cycle that lost the
/// clock, with @p restoredOn as the date stored in the recovery file.
DailyLedger bootWithUnsetClock(const std::tm& restoredOn, bool hadRecovery = true)
{
    DailyLedger l;
    const auto ro = l.bootRestore(DailyDate::fromTm(kUnsetNow), hadRecovery,
                                  restored(), restoredOn, /*clockValid*/ false);
    EXPECT_FALSE(ro.archiveStale);
    return l;
}

} // namespace

TEST(DailyLedger, UnsetClockAtBootKeepsTheRestoredDay)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 13, 14, 20));

    EXPECT_FALSE(l.dateKnown());
    EXPECT_TRUE(l.date().sameDay(ddAt(2026, 10, 13)));
    EXPECT_EQ(l.metrics().steps, 5000u);
    EXPECT_FALSE(l.dirty());
}

TEST(DailyLedger, TicksWithUnsetClockPublishButBookNothing)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 13, 14, 20));

    auto out = l.onMinuteTick(kUnsetNow, snap(120, 80.0f), /*clockValid*/ false);

    EXPECT_FALSE(out.dayRolled);
    EXPECT_EQ(out.publish.steps, 5120u);
    // HR covers the whole restored day, not 00:00-00:03 of the bogus clock.
    EXPECT_EQ(out.publish.averageHeartRate, 70u);
    EXPECT_EQ(l.composeRecord(kUnsetNow).steps, 5120u);

    EXPECT_EQ(l.metrics().steps, 5000u);
    EXPECT_EQ(l.metrics().hrPerMinute[0][3], 0u);
    EXPECT_FALSE(l.dirty());
}

TEST(DailyLedger, ClockSetOnTheSameDayContinuesTheDay)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 13, 14, 20));
    l.onMinuteTick(kUnsetNow, snap(120), false);

    auto out = l.onMinuteTick(tmAt(2026, 10, 13, 14, 30), snap(150), true);

    EXPECT_FALSE(out.dayRolled);
    EXPECT_TRUE(l.dateKnown());
    EXPECT_EQ(l.metrics().steps, 5150u);
    EXPECT_EQ(out.publish.steps, 5150u);
    EXPECT_TRUE(l.dirty());

    // The pending count is gone: the next tick books only its own delta.
    out = l.onMinuteTick(tmAt(2026, 10, 13, 14, 31), snap(160), true);
    EXPECT_EQ(out.publish.steps, 5160u);
}

TEST(DailyLedger, ClockSetOnALaterDayArchivesTheRestoredDay)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 12, 23, 50));
    l.onMinuteTick(kUnsetNow, snap(40), false);

    auto out = l.onMinuteTick(tmAt(2026, 10, 13, 7, 0), snap(60), true);

    ASSERT_TRUE(out.dayRolled);
    EXPECT_EQ(l.finishedTm().tm_mday, 12);
    EXPECT_EQ(l.finishedRecord().steps, 5000u);
    EXPECT_EQ(l.finishedMetrics().hrPerMinute[9][0], 70u);

    // Steps since the reboot go to the day the clock says it is.
    EXPECT_TRUE(l.date().sameDay(ddAt(2026, 10, 13)));
    EXPECT_EQ(l.metrics().steps, 60u);
    EXPECT_EQ(out.publish.steps, 60u);
    EXPECT_TRUE(l.dirty());
}

TEST(DailyLedger, ClockSetWithoutARecoveryArchivesNothing)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 13, 14, 20),
                                       /*hadRecovery*/ false);
    EXPECT_EQ(l.metrics().steps, 0u);
    l.onMinuteTick(kUnsetNow, snap(40), false);

    auto out = l.onMinuteTick(tmAt(2026, 10, 13, 7, 0), snap(60), true);

    EXPECT_FALSE(out.dayRolled);
    EXPECT_TRUE(l.date().sameDay(ddAt(2026, 10, 13)));
    EXPECT_EQ(l.metrics().steps, 60u);
}

TEST(DailyLedger, ValidClockStillArchivesAStaleRecovery)
{
    DailyLedger l;
    const auto ro = l.bootRestore(ddAt(2026, 10, 13), true, restored(),
                                  tmAt(2026, 10, 12, 23, 50), /*clockValid*/ true);

    EXPECT_TRUE(ro.archiveStale);
    EXPECT_EQ(l.finishedRecord().steps, 5000u);
    EXPECT_TRUE(l.dateKnown());
    EXPECT_EQ(l.metrics().steps, 0u);
}

TEST(DailyLedger, ValidClockRestoresToday)
{
    DailyLedger l;
    const auto ro = l.bootRestore(ddAt(2026, 10, 13), true, restored(),
                                  tmAt(2026, 10, 13, 14, 20), true);

    EXPECT_FALSE(ro.archiveStale);
    auto out = l.onMinuteTick(tmAt(2026, 10, 13, 14, 30), snap(25), true);
    EXPECT_FALSE(out.dayRolled);
    EXPECT_EQ(l.metrics().steps, 5025u);
}

TEST(DailyLedger, ResetWithUnsetClockLeavesTheDateUnconfirmed)
{
    DailyLedger l = bootWithUnsetClock(tmAt(2026, 10, 13, 14, 20));
    l.resetTo(DailyDate::fromTm(kUnsetNow), snap(100), /*clockValid*/ false);

    EXPECT_FALSE(l.dateKnown());
    EXPECT_EQ(l.composeRecord(kUnsetNow).steps, 0u);

    // Nothing to archive once the clock is set; only post-reset steps count.
    auto out = l.onMinuteTick(tmAt(2026, 10, 13, 15, 0), snap(130), true);
    EXPECT_FALSE(out.dayRolled);
    EXPECT_EQ(l.metrics().steps, 30u);
}
