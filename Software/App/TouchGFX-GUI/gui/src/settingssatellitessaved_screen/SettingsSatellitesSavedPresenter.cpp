#include <gui/settingssatellitessaved_screen/SettingsSatellitesSavedView.hpp>
#include <gui/settingssatellitessaved_screen/SettingsSatellitesSavedPresenter.hpp>

SettingsSatellitesSavedPresenter::SettingsSatellitesSavedPresenter(SettingsSatellitesSavedView& v)
    : view(v)
{

}

void SettingsSatellitesSavedPresenter::activate()
{
    view.setGpsPowerMode(model->getLocalSettings().gpsPowerMode);
}

void SettingsSatellitesSavedPresenter::deactivate()
{

}
