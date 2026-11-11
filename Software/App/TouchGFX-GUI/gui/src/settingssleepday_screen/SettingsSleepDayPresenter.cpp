#include <gui/settingssleepday_screen/SettingsSleepDayView.hpp>
#include <gui/settingssleepday_screen/SettingsSleepDayPresenter.hpp>

namespace SleepMenu = Gui::Config::Menu::Top::Settings::Backlight::Sleep;

SettingsSleepDayPresenter::SettingsSleepDayPresenter(SettingsSleepDayView& v)
    : view(v)
{

}

void SettingsSleepDayPresenter::activate()
{
    const WatchSettingsLocal &sett = model->getLocalSettings();
    const uint8_t day = model->getSleepEditDay() % WatchSettingsLocal::Sleep::kDays;

    view.set(day, sett.sleep.days[day].startMin, sett.sleep.days[day].endMin,
             sett.clock.format == WatchSettingsLocal::Clock::FMT_12H);

    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsSleepDayPresenter::deactivate()
{

}

void SettingsSleepDayPresenter::save(uint8_t day, uint16_t startMin, uint16_t endMin,
                                     SleepMenu::ApplyTo applyTo)
{
    using Sleep = WatchSettingsLocal::Sleep;
    constexpr uint8_t kSaturday = 5;    // days[] is Monday first

    WatchSettingsLocal sett = model->getLocalSettings();

    for (uint8_t d = 0; d < Sleep::kDays; ++d) {
        bool apply = false;
        switch (applyTo) {
            case SleepMenu::APPLY_THIS_DAY: apply = (d == day);       break;
            case SleepMenu::APPLY_WEEKDAYS: apply = (d < kSaturday);  break;
            case SleepMenu::APPLY_WEEKEND:  apply = (d >= kSaturday); break;
            case SleepMenu::APPLY_ALL_DAYS: apply = true;             break;
            default: break;
        }

        if (apply) {
            sett.sleep.days[d].startMin = startMin;
            sett.sleep.days[d].endMin = endMin;
        }
    }

    model->saveLocalSettings(sett);
}
