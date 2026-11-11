#include <gui/settingsbacklight_screen/SettingsBacklightView.hpp>
#include <gui/settingsbacklight_screen/SettingsBacklightPresenter.hpp>

SettingsBacklightPresenter::SettingsBacklightPresenter(SettingsBacklightView& v)
    : view(v)
{

}

void SettingsBacklightPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuBacklight());

    // Reset nested menu position
    model->setPosMenuSleep(Gui::Config::Menu::Top::Settings::Backlight::Sleep::ID_DEFAULT);

    // Reset idle timer
    model->resetIdleTimer();

    view.setGesture(model->getLocalSettings().backlight.gesture);
    view.setTimeout(model->getLocalSettings().backlight.timeoutS);
    view.setSchedule(model->getLocalSettings().sleep.schedule);
}

void SettingsBacklightPresenter::deactivate()
{
    model->setPosMenuBacklight(view.getPositionId());
}

void SettingsBacklightPresenter::saveGesture(bool on)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.backlight.gesture = on;
    model->saveLocalSettings(sett);
}
