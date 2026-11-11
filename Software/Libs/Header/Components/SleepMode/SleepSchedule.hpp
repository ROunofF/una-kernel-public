/**
 ******************************************************************************
 * @file    SleepSchedule.hpp
 * @brief   Sleep mode: what it changes, and when the schedule switches it.
 ******************************************************************************
 * Sleep mode turns lift-to-wake off and mutes alerts; lift-to-wake also needs
 * the Gesture setting on. Sleep mode is switched by hand
 * from the top menu, or by a per-day schedule of sleep windows.
 *
 * The schedule acts on its own transitions only. Sleep mode follows it when the
 * schedule's answer changes -- a window starts or ends, the schedule is enabled,
 * or a window is edited across the current time -- and not otherwise. So a
 * change made by hand holds until the next time the schedule changes its mind.
 * WatchSettingsLocal::Sleep::scheduledActive records the last answer, and is
 * persisted with the rest, so a reboot inside a window does not count as one
 * starting.
 *
 * Pure functions over WatchSettingsLocal, so the host tests can drive them.
 ******************************************************************************
 */

#ifndef SLEEP_MODE_SLEEP_SCHEDULE_HPP
#define SLEEP_MODE_SLEEP_SCHEDULE_HPP

#include <cstdint>

// By relative path: the GUI simulator uses this too, and does not carry
// Components/Shared on its include path.
#include "../Shared/WatchSettingsLocal.hpp"

namespace SleepMode
{

using Sleep = WatchSettingsLocal::Sleep;

/**
 * @brief   Day index into Sleep::days from a struct tm weekday.
 * @param   tmWday: 0 for Sunday to 6 for Saturday, as std::tm::tm_wday.
 * @retval  0 for Monday to 6 for Sunday.
 */
constexpr uint8_t dayFromTmWday(int tmWday)
{
    return static_cast<uint8_t>((tmWday + 6) % 7);
}

/**
 * @brief   Whether a sleep window covers a local time.
 * @param   sleep: The windows; whether the schedule is enabled is not looked at.
 * @param   day: 0 for Monday to 6 for Sunday.
 * @param   minute: Minutes after local midnight, below Sleep::kMinutesPerDay.
 * @retval  'true' if today's window, or yesterday's running past midnight,
 *          covers the time.
 */
inline bool inWindow(const Sleep &sleep, uint8_t day, uint16_t minute)
{
    const Sleep::Window &today = sleep.days[day % Sleep::kDays];
    if (today.startMin < today.endMin) {
        if (minute >= today.startMin && minute < today.endMin) {
            return true;
        }
    } else if (today.startMin > today.endMin) {
        // Runs past midnight: today only holds the part before it.
        if (minute >= today.startMin) {
            return true;
        }
    }

    const Sleep::Window &yesterday =
            sleep.days[(day + Sleep::kDays - 1) % Sleep::kDays];
    return (yesterday.startMin > yesterday.endMin && minute < yesterday.endMin);
}

/**
 * @brief   Whether lift-to-wake should be running.
 * @retval  'true' when the Gesture setting is on and sleep mode is off.
 */
inline bool liftToWake(const WatchSettingsLocal &settings)
{
    return settings.backlight.gesture && !settings.sleep.active;
}

/**
 * @brief   Start or end sleep mode.
 * @details Starting mutes alerts, keeping what the mute was to put back at the
 *          end. Asking for the state it is already in changes nothing, so the
 *          mute kept at the start is not overwritten by one taken mid-sleep.
 * @retval  'true' if the settings changed.
 */
inline bool set(WatchSettingsLocal &settings, bool on)
{
    Sleep &sleep = settings.sleep;
    if (sleep.active == on) {
        return false;
    }

    if (on) {
        sleep.alertsMutedBefore = settings.alertMutedFlag;
        settings.alertMutedFlag = true;
    } else {
        settings.alertMutedFlag = sleep.alertsMutedBefore;
    }
    sleep.active = on;
    return true;
}

/**
 * @brief   Let the schedule act at a local time.
 * @details Sleep mode follows the schedule only when the schedule's answer
 *          differs from the one it last gave. A disabled schedule answers
 *          "awake" but never ends sleep mode: turning it off leaves the state
 *          where it is, and the top menu switches it from there.
 * @param   day: 0 for Monday to 6 for Sunday.
 * @param   minute: Minutes after local midnight.
 * @retval  'true' if the settings changed and want saving.
 */
inline bool evaluate(WatchSettingsLocal &settings, uint8_t day, uint16_t minute)
{
    Sleep &sleep = settings.sleep;
    const bool want = sleep.schedule && inWindow(sleep, day, minute);
    if (want == sleep.scheduledActive) {
        return false;
    }

    sleep.scheduledActive = want;
    if (sleep.schedule) {
        set(settings, want);
    }
    return true;
}

/**
 * @brief   Merge a copy of the settings from the GUI with the backend's own.
 * @details The GUI saves the whole struct, from a copy that may predate the
 *          backend's last change of sleep state. Sleep state is the backend's,
 *          so it is kept from @p current; everything else is taken from @p
 *          incoming, alert mute included (the top menu can change it mid-sleep).
 * @retval  The settings to save.
 */
inline WatchSettingsLocal mergeFromGui(const WatchSettingsLocal &current,
                                       const WatchSettingsLocal &incoming)
{
    WatchSettingsLocal merged = incoming;
    merged.sleep.active            = current.sleep.active;
    merged.sleep.scheduledActive   = current.sleep.scheduledActive;
    merged.sleep.alertsMutedBefore = current.sleep.alertsMutedBefore;
    return merged;
}

} // namespace SleepMode

#endif // SLEEP_MODE_SLEEP_SCHEDULE_HPP
