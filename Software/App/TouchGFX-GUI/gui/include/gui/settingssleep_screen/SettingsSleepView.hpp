#ifndef SETTINGSSLEEPVIEW_HPP
#define SETTINGSSLEEPVIEW_HPP

#include <gui_generated/settingssleep_screen/SettingsSleepViewBase.hpp>
#include <gui/settingssleep_screen/SettingsSleepPresenter.hpp>

/**
 * @brief Settings > Backlight > Sleep Mode: the schedule switch, then each
 *        day's sleep window, Monday first. A day opens the window editor.
 */
class SettingsSleepView : public SettingsSleepViewBase
{
public:
    SettingsSleepView();
    virtual ~SettingsSleepView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    void setSchedule(bool on);
    void setWindows(const WatchSettingsLocal::Sleep &sleep, bool twelveHour);

protected:
    using Sleep = WatchSettingsLocal::Sleep;

    bool mSchedule = false;

    /// The day items' tips, "22:00-07:00". The menu points into these.
    static constexpr uint16_t kWindowTextSize = 16;
    touchgfx::Unicode::UnicodeChar mWindowText[Sleep::kDays][kWindowTextSize] {};

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSSLEEPVIEW_HPP
