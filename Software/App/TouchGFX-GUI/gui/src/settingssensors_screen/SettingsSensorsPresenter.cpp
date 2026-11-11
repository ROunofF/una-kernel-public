#include <gui/settingssensors_screen/SettingsSensorsView.hpp>
#include <gui/settingssensors_screen/SettingsSensorsPresenter.hpp>

SettingsSensorsPresenter::SettingsSensorsPresenter(SettingsSensorsView& v)
    : view(v)
{

}

void SettingsSensorsPresenter::activate()
{
    view.showConfig(model->accessoryConfig());
    view.showScanResults(model->accessoryScanResults());
    view.setPositionId(model->getPosMenuSensors());
}

void SettingsSensorsPresenter::deactivate()
{
    // Leaving the screen must stop any discovery scan in progress.
    model->accessoryScanStop();

    // Remembered so that a screen opened from a row returns to that row.
    model->setPosMenuSensors(view.getSensorsRow());
}

void SettingsSensorsPresenter::onAccessoryConfig()
{
    view.showConfig(model->accessoryConfig());
}

void SettingsSensorsPresenter::onAccessoryScanResult()
{
    view.showScanResults(model->accessoryScanResults());
}

void SettingsSensorsPresenter::onAccessoryStatus()
{
    // Settings ▸ Sensors is config-only: live link status is
    // shown on the pre-activity/in-workout indicator, not here. The
    // model still tracks status for that path; this screen ignores it.
}

void SettingsSensorsPresenter::accessorySetMode(uint8_t mode)
{
    model->accessorySetMode(mode);
}

void SettingsSensorsPresenter::accessoryScanStart()
{
    model->accessoryScanStart();
}

void SettingsSensorsPresenter::accessoryScanStop()
{
    model->accessoryScanStop();
}

void SettingsSensorsPresenter::accessoryForget(uint8_t index)
{
    model->accessoryForget(index);
}

void SettingsSensorsPresenter::accessoryRemember(const B2GEvent::AccessoryScanResult& dev)
{
    model->accessoryRemember(dev);
}
