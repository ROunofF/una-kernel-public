#include <gui/settingsgnss_screen/SettingsGNSSView.hpp>
#include <gui/settingsgnss_screen/SettingsGNSSPresenter.hpp>

SettingsGNSSPresenter::SettingsGNSSPresenter(SettingsGNSSView& v)
    : view(v)
{

}

void SettingsGNSSPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuGNSS());

    // Reset idle timer
    model->resetIdleTimer();

    view.setGpsPowerMode(model->getLocalSettings().gpsPowerMode);
}

void SettingsGNSSPresenter::deactivate()
{
    model->setPosMenuGNSS(view.getPositionId());
}
