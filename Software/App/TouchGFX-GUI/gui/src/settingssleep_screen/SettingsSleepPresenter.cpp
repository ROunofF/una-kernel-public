#include <gui/settingssleep_screen/SettingsSleepView.hpp>
#include <gui/settingssleep_screen/SettingsSleepPresenter.hpp>

SettingsSleepPresenter::SettingsSleepPresenter(SettingsSleepView& v)
    : view(v)
{

}

void SettingsSleepPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuSleep());

    // Reset idle timer
    model->resetIdleTimer();

    onLocalSettings(model->getLocalSettings());
}

void SettingsSleepPresenter::deactivate()
{
    model->setPosMenuSleep(view.getPositionId());
}

void SettingsSleepPresenter::onLocalSettings(const WatchSettingsLocal &sett)
{
    view.setSchedule(sett.sleep.schedule);
    view.setWindows(sett.sleep, sett.clock.format == WatchSettingsLocal::Clock::FMT_12H);
}

void SettingsSleepPresenter::saveSchedule(bool on)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.sleep.schedule = on;
    model->saveLocalSettings(sett);
}
