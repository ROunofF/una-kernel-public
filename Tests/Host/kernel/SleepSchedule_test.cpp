/**
 ******************************************************************************
 * @file    SleepSchedule_test.cpp
 * @brief   Host tests for SleepMode: which times a window covers, what sleep
 *          mode does to the alert mute, and that the schedule acts only on its
 *          own transitions, so a change made by hand holds until the next one.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "SleepMode/SleepSchedule.hpp"

namespace {

using Sleep = WatchSettingsLocal::Sleep;

constexpr uint8_t kMon = 0;
constexpr uint8_t kTue = 1;
constexpr uint8_t kSun = 6;

constexpr uint16_t hm(int h, int m)
{
    return static_cast<uint16_t>(h * 60 + m);
}

WatchSettingsLocal scheduled()
{
    WatchSettingsLocal s;
    s.sleep.schedule = true;    // every day 22:00-07:00 by default
    return s;
}

// -- dayFromTmWday -----------------------------------------------------------

TEST(SleepSchedule, DayIndexStartsOnMonday)
{
    EXPECT_EQ(SleepMode::dayFromTmWday(1), kMon);   // tm_wday Monday
    EXPECT_EQ(SleepMode::dayFromTmWday(0), kSun);   // tm_wday Sunday
    EXPECT_EQ(SleepMode::dayFromTmWday(6), 5);      // Saturday
}

// -- inWindow ----------------------------------------------------------------

TEST(SleepSchedule, WindowAcrossMidnightBelongsToItsStartDay)
{
    Sleep s;
    s.days[kMon] = { hm(22, 0), hm(7, 0) };
    s.days[kTue] = { hm(23, 0), hm(6, 0) };

    EXPECT_FALSE(SleepMode::inWindow(s, kMon, hm(21, 59)));
    EXPECT_TRUE(SleepMode::inWindow(s, kMon, hm(22, 0)));
    EXPECT_TRUE(SleepMode::inWindow(s, kMon, hm(23, 59)));

    // Tuesday morning is Monday night's window, not Tuesday's.
    EXPECT_TRUE(SleepMode::inWindow(s, kTue, hm(6, 30)));
    EXPECT_FALSE(SleepMode::inWindow(s, kTue, hm(7, 0)));
    EXPECT_FALSE(SleepMode::inWindow(s, kTue, hm(22, 30)));
    EXPECT_TRUE(SleepMode::inWindow(s, kTue, hm(23, 0)));
}

TEST(SleepSchedule, MondayMorningIsSundayNight)
{
    Sleep s;
    s.days[kSun] = { hm(21, 0), hm(8, 0) };
    s.days[kMon] = { hm(23, 0), hm(6, 0) };

    EXPECT_TRUE(SleepMode::inWindow(s, kMon, hm(7, 30)));
    EXPECT_FALSE(SleepMode::inWindow(s, kMon, hm(8, 0)));
}

TEST(SleepSchedule, WindowWithinOneDay)
{
    Sleep s;
    s.days[kMon] = { hm(13, 0), hm(14, 30) };
    s.days[kSun] = { hm(13, 0), hm(14, 30) };   // nothing spills into Monday

    EXPECT_FALSE(SleepMode::inWindow(s, kMon, hm(0, 0)));
    EXPECT_TRUE(SleepMode::inWindow(s, kMon, hm(13, 0)));
    EXPECT_FALSE(SleepMode::inWindow(s, kMon, hm(14, 30)));
}

TEST(SleepSchedule, EqualStartAndEndIsEmpty)
{
    Sleep s;
    for (Sleep::Window &w : s.days) {
        w = { hm(22, 0), hm(22, 0) };
    }

    for (uint16_t m = 0; m < Sleep::kMinutesPerDay; m += 7) {
        EXPECT_FALSE(SleepMode::inWindow(s, kMon, m)) << m;
    }
}

// -- set ---------------------------------------------------------------------

TEST(SleepSchedule, SleepMutesAlertsAndPutsTheMuteBack)
{
    WatchSettingsLocal s;
    s.alertMutedFlag = false;

    EXPECT_TRUE(SleepMode::set(s, true));
    EXPECT_TRUE(s.sleep.active);
    EXPECT_TRUE(s.alertMutedFlag);

    EXPECT_TRUE(SleepMode::set(s, false));
    EXPECT_FALSE(s.sleep.active);
    EXPECT_FALSE(s.alertMutedFlag);
}

TEST(SleepSchedule, AlertsMutedBeforeSleepStayMuted)
{
    WatchSettingsLocal s;
    s.alertMutedFlag = true;

    SleepMode::set(s, true);
    SleepMode::set(s, false);
    EXPECT_TRUE(s.alertMutedFlag);
}

TEST(SleepSchedule, AlertsChangedDuringSleepAreRestoredToPreSleep)
{
    WatchSettingsLocal s;
    s.alertMutedFlag = false;

    SleepMode::set(s, true);
    s.alertMutedFlag = false;   // unmuted from the top menu mid-sleep
    SleepMode::set(s, false);
    EXPECT_FALSE(s.alertMutedFlag);

    s.alertMutedFlag = true;
    SleepMode::set(s, true);
    s.alertMutedFlag = false;
    SleepMode::set(s, false);
    EXPECT_TRUE(s.alertMutedFlag);
}

TEST(SleepSchedule, SettingTheSameStateChangesNothing)
{
    WatchSettingsLocal s;
    s.alertMutedFlag = false;
    SleepMode::set(s, true);

    // A second start must not take the sleep-time mute as the one to restore.
    EXPECT_FALSE(SleepMode::set(s, true));
    SleepMode::set(s, false);
    EXPECT_FALSE(s.alertMutedFlag);

    EXPECT_FALSE(SleepMode::set(s, false));
}

// -- evaluate ----------------------------------------------------------------

TEST(SleepSchedule, WindowStartsAndEndsSleep)
{
    WatchSettingsLocal s = scheduled();

    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(21, 59)));
    EXPECT_FALSE(s.sleep.active);

    EXPECT_TRUE(SleepMode::evaluate(s, kMon, hm(22, 0)));
    EXPECT_TRUE(s.sleep.active);
    EXPECT_TRUE(s.alertMutedFlag);

    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(23, 0)));

    EXPECT_TRUE(SleepMode::evaluate(s, kTue, hm(7, 0)));
    EXPECT_FALSE(s.sleep.active);
    EXPECT_FALSE(s.alertMutedFlag);
}

TEST(SleepSchedule, WakingByHandHoldsUntilTheWindowEnds)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(22, 0));

    SleepMode::set(s, false);   // top menu, 23:00
    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(23, 30)));
    EXPECT_FALSE(SleepMode::evaluate(s, kTue, hm(3, 0)));
    EXPECT_FALSE(s.sleep.active);

    // The window ends: awake already, and the next one starts as usual.
    EXPECT_TRUE(SleepMode::evaluate(s, kTue, hm(7, 0)));
    EXPECT_FALSE(s.sleep.active);
    EXPECT_TRUE(SleepMode::evaluate(s, kTue, hm(22, 0)));
    EXPECT_TRUE(s.sleep.active);
}

TEST(SleepSchedule, SleepingByHandHoldsUntilTheNextScheduledChange)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(12, 0));

    SleepMode::set(s, true);    // top menu, afternoon nap
    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(15, 0)));
    EXPECT_TRUE(s.sleep.active);

    // The window starts while already asleep, then ends: awake.
    SleepMode::evaluate(s, kMon, hm(22, 0));
    EXPECT_TRUE(s.sleep.active);
    SleepMode::evaluate(s, kTue, hm(7, 0));
    EXPECT_FALSE(s.sleep.active);
    EXPECT_FALSE(s.alertMutedFlag);
}

TEST(SleepSchedule, RebootInsideAWindowIsNotAStart)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(22, 0));
    SleepMode::set(s, false);   // woken by hand

    // Persisted and read back after a reboot at 02:00: no change.
    WatchSettingsLocal rebooted = s;
    EXPECT_FALSE(SleepMode::evaluate(rebooted, kTue, hm(2, 0)));
    EXPECT_FALSE(rebooted.sleep.active);
}

TEST(SleepSchedule, WindowMissedWhileOffIsCaughtUp)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(20, 0));

    // Off from 20:00 until 23:00: the start was missed but is acted on.
    EXPECT_TRUE(SleepMode::evaluate(s, kMon, hm(23, 0)));
    EXPECT_TRUE(s.sleep.active);
}

TEST(SleepSchedule, EnablingInsideAWindowSleepsNow)
{
    WatchSettingsLocal s;
    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(23, 0)));
    EXPECT_FALSE(s.sleep.active);

    s.sleep.schedule = true;
    EXPECT_TRUE(SleepMode::evaluate(s, kMon, hm(23, 0)));
    EXPECT_TRUE(s.sleep.active);
}

TEST(SleepSchedule, EnablingOutsideAWindowKeepsSleepByHand)
{
    WatchSettingsLocal s;
    SleepMode::set(s, true);

    s.sleep.schedule = true;
    EXPECT_FALSE(SleepMode::evaluate(s, kMon, hm(12, 0)));
    EXPECT_TRUE(s.sleep.active);
}

TEST(SleepSchedule, DisablingLeavesTheStateAlone)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(23, 0));

    s.sleep.schedule = false;
    EXPECT_TRUE(SleepMode::evaluate(s, kMon, hm(23, 1)));   // bookkeeping only
    EXPECT_TRUE(s.sleep.active);
    EXPECT_FALSE(s.sleep.scheduledActive);

    // And nothing ends it at what would have been the window's end.
    EXPECT_FALSE(SleepMode::evaluate(s, kTue, hm(7, 0)));
    EXPECT_TRUE(s.sleep.active);
}

TEST(SleepSchedule, EditingAWindowOverNowActsOnIt)
{
    WatchSettingsLocal s = scheduled();
    SleepMode::evaluate(s, kMon, hm(21, 0));
    EXPECT_FALSE(s.sleep.active);

    s.sleep.days[kMon].startMin = hm(20, 30);
    EXPECT_TRUE(SleepMode::evaluate(s, kMon, hm(21, 0)));
    EXPECT_TRUE(s.sleep.active);
}

// -- mergeFromGui ------------------------------------------------------------

TEST(SleepSchedule, GuiCopyCannotOverwriteSleepState)
{
    WatchSettingsLocal current = scheduled();
    SleepMode::evaluate(current, kMon, hm(22, 0));

    // A copy taken before the window started, with a new timeout on it.
    WatchSettingsLocal stale = scheduled();
    stale.backlight.timeoutS = 10;

    const WatchSettingsLocal merged = SleepMode::mergeFromGui(current, stale);
    EXPECT_EQ(merged.backlight.timeoutS, 10);
    EXPECT_TRUE(merged.sleep.active);
    EXPECT_TRUE(merged.sleep.scheduledActive);
    EXPECT_EQ(merged.sleep.alertsMutedBefore, current.sleep.alertsMutedBefore);
}

TEST(SleepSchedule, GuiCopyCarriesScheduleEdits)
{
    WatchSettingsLocal current = scheduled();
    WatchSettingsLocal edited = current;
    edited.sleep.days[kTue] = { hm(23, 15), hm(6, 45) };
    edited.sleep.schedule = false;

    const WatchSettingsLocal merged = SleepMode::mergeFromGui(current, edited);
    EXPECT_EQ(merged.sleep.days[kTue], edited.sleep.days[kTue]);
    EXPECT_FALSE(merged.sleep.schedule);
}

TEST(SleepSchedule, GuiCopyCarriesGesture)
{
    WatchSettingsLocal current = scheduled();
    WatchSettingsLocal edited = current;
    edited.backlight.gesture = false;

    EXPECT_FALSE(SleepMode::mergeFromGui(current, edited).backlight.gesture);
}

// -- liftToWake --------------------------------------------------------------

TEST(SleepSchedule, LiftToWakeOnByDefault)
{
    EXPECT_TRUE(SleepMode::liftToWake(WatchSettingsLocal {}));
}

TEST(SleepSchedule, LiftToWakeNeedsGestureAndAwake)
{
    WatchSettingsLocal s;

    s.backlight.gesture = false;
    EXPECT_FALSE(SleepMode::liftToWake(s));

    s.backlight.gesture = true;
    SleepMode::set(s, true);
    EXPECT_FALSE(SleepMode::liftToWake(s));

    // Gesture off stays off when sleep ends.
    s.backlight.gesture = false;
    SleepMode::set(s, false);
    EXPECT_FALSE(SleepMode::liftToWake(s));

    s.backlight.gesture = true;
    EXPECT_TRUE(SleepMode::liftToWake(s));
}

} // namespace
