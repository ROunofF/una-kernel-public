#include <gui/settings_screen/SettingsView.hpp>
#include <gui/settings_screen/SettingsPresenter.hpp>

SettingsPresenter::SettingsPresenter(SettingsView& v)
    : view(v)
{

}

void SettingsPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuTopSettings());

    // Reset nested menu position
    model->setPosMenuGNSS(Gui::Config::Menu::Top::Settings::GNSS::ID_DEFAULT);
    model->setPosMenuClock(Gui::Config::Menu::Top::Settings::Clock::ID_DEFAULT);
    model->setPosMenuBacklight(Gui::Config::Menu::Top::Settings::Backlight::ID_DEFAULT);
    model->setPosMenuSleep(Gui::Config::Menu::Top::Settings::Backlight::Sleep::ID_DEFAULT);
    model->setPosMenuSensors(0);

    // Reset idle timer
    model->resetIdleTimer();

    view.bonded(model->bleIsBonded());
}

void SettingsPresenter::deactivate()
{
    model->setPosMenuTopSettings(view.getPositionId());
}