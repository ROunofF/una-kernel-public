#include <gui/settingsdevice_screen/SettingsDeviceView.hpp>
#include <gui/settingsdevice_screen/SettingsDevicePresenter.hpp>

SettingsDevicePresenter::SettingsDevicePresenter(SettingsDeviceView& v)
    : view(v)
{

}

void SettingsDevicePresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    view.setInfo(model->getDeviceInfo());
}

void SettingsDevicePresenter::deactivate()
{

}
