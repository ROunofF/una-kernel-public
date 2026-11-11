#include <gui/settingssounds_screen/SettingsSoundsView.hpp>
#include <gui/settingssounds_screen/SettingsSoundsPresenter.hpp>

SettingsSoundsPresenter::SettingsSoundsPresenter(SettingsSoundsView& v)
    : view(v)
{

}

void SettingsSoundsPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    view.setButtons(model->getLocalSettings().sound.buttons);
    view.setMsgCalls(model->getLocalSettings().sound.msgCalls);
    view.setAlerts(model->getLocalSettings().sound.alerts);
}

void SettingsSoundsPresenter::deactivate()
{

}

void SettingsSoundsPresenter::saveButtons(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.sound.buttons = state;
    model->saveLocalSettings(sett);
}

void SettingsSoundsPresenter::saveMsgCalls(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.sound.msgCalls = state;
    model->saveLocalSettings(sett);
}

void SettingsSoundsPresenter::saveAlerts(bool state)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.sound.alerts = state;
    model->saveLocalSettings(sett);
}
