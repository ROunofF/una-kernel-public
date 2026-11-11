#include <gui/settingssatellites_screen/SettingsSatellitesView.hpp>
#include <gui/settingssatellites_screen/SettingsSatellitesPresenter.hpp>

SettingsSatellitesPresenter::SettingsSatellitesPresenter(SettingsSatellitesView& v)
    : view(v)
{

}

void SettingsSatellitesPresenter::activate()
{
    // Set current menu position
    view.setGpsPowerMode(model->getLocalSettings().gpsPowerMode);

    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsSatellitesPresenter::deactivate()
{

}

void SettingsSatellitesPresenter::saveGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.gpsPowerMode = mode;
    model->saveLocalSettings(sett);
}