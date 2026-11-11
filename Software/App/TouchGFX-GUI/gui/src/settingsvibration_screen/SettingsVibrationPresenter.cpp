#include <gui/settingsvibration_screen/SettingsVibrationView.hpp>
#include <gui/settingsvibration_screen/SettingsVibrationPresenter.hpp>

SettingsVibrationPresenter::SettingsVibrationPresenter(SettingsVibrationView& v)
    : view(v)
{

}

void SettingsVibrationPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    view.setButtons(model->getLocalSettings().vibro.buttons);
    view.setMsgCalls(model->getLocalSettings().vibro.msgCalls);
    view.setAlerts(model->getLocalSettings().vibro.alerts);
}

void SettingsVibrationPresenter::deactivate()
{

}

void SettingsVibrationPresenter::saveButtons(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.vibro.buttons = state;
    model->saveLocalSettings(sett);
}

void SettingsVibrationPresenter::saveMsgCalls(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.vibro.msgCalls = state;
    model->saveLocalSettings(sett);
}

void SettingsVibrationPresenter::saveAlerts(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.vibro.alerts = state;
    model->saveLocalSettings(sett);
}
