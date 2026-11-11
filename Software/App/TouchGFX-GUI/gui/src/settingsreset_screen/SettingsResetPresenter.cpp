#include <gui/settingsreset_screen/SettingsResetView.hpp>
#include <gui/settingsreset_screen/SettingsResetPresenter.hpp>

SettingsResetPresenter::SettingsResetPresenter(SettingsResetView& v)
    : view(v)
{

}

void SettingsResetPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsResetPresenter::deactivate()
{

}

void SettingsResetPresenter::reset()
{
    model->resetSettings();
}