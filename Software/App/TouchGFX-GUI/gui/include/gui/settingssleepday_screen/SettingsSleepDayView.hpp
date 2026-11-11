#ifndef SETTINGSSLEEPDAYVIEW_HPP
#define SETTINGSSLEEPDAYVIEW_HPP

#include <gui_generated/settingssleepday_screen/SettingsSleepDayViewBase.hpp>
#include <gui/settingssleepday_screen/SettingsSleepDayPresenter.hpp>

/**
 * @brief Edit one day's sleep window, then choose which days it applies to.
 *
 * Steps through, R1 next and R2 back (R2 on the first step leaves unsaved):
 *   start hours -> start min. -> [start AM/PM] -> end hours -> end min. ->
 *   [end AM/PM] -> apply to
 * The AM/PM steps are only visited when the clock is 12-hour. R1 on the last
 * step saves. The time steps use the hour/minute wheel; the AM/PM and "apply
 * to" steps the standard menu, which covers the rest of the screen.
 */
class SettingsSleepDayView : public SettingsSleepDayViewBase
{
public:
    SettingsSleepDayView();
    virtual ~SettingsSleepDayView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /**
     * @brief Start editing a day's window.
     * @param day: 0 for Monday.
     * @param startMin, endMin: the window, in minutes after midnight.
     * @param twelveHour: edit hours as 12, 1 .. 11 with an AM/PM step.
     */
    void set(uint8_t day, uint16_t startMin, uint16_t endMin, bool twelveHour);

protected:
    enum Step : uint8_t {
        START_HOURS = 0,
        START_MINUTES,
        START_AMPM,
        END_HOURS,
        END_MINUTES,
        END_AMPM,
        APPLY_TO,
    };

    Step mStep = START_HOURS;
    uint8_t mDay = 0;
    bool m12Hour = false;
    uint16_t mStartMin = 0;
    uint16_t mEndMin = 0;

    /// The day's name in capitals, for the title.
    static constexpr uint16_t kTitleSize = 16;
    touchgfx::Unicode::UnicodeChar mTitle[kTitleSize] {};

    static bool isEnd(Step step) { return step >= END_HOURS && step <= END_AMPM; }
    static bool isTime(Step step) { return step != START_AMPM && step != END_AMPM && step != APPLY_TO; }

    /// The window bound a step edits.
    uint16_t &bound(Step step) { return isEnd(step) ? mEndMin : mStartMin; }

    /// The step after (or before) this one, skipping AM/PM in 24-hour mode.
    Step next(Step step) const;
    Step prev(Step step) const;

    /// Take what the step on screen shows into mStartMin / mEndMin.
    void capture();
    /// Put a step on screen.
    void show(Step step);

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSSLEEPDAYVIEW_HPP
