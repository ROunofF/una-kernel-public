#include <gui/settingsbacklighttimeout_screen/SettingsBacklightTimeoutView.hpp>
#include <gui/settingsbacklighttimeout_screen/SettingsBacklightTimeoutPresenter.hpp>

SettingsBacklightTimeoutPresenter::SettingsBacklightTimeoutPresenter(SettingsBacklightTimeoutView& v)
    : view(v)
{

}

void SettingsBacklightTimeoutPresenter::activate()
{
    view.setTimeout(model->getLocalSettings().backlight.timeoutS);

    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsBacklightTimeoutPresenter::deactivate()
{

}

void SettingsBacklightTimeoutPresenter::saveTimeout(uint8_t seconds)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.backlight.timeoutS = seconds;
    model->saveLocalSettings(sett);
}
