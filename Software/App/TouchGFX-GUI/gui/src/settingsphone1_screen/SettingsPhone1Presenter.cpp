#include <gui/settingsphone1_screen/SettingsPhone1View.hpp>
#include <gui/settingsphone1_screen/SettingsPhone1Presenter.hpp>

SettingsPhone1Presenter::SettingsPhone1Presenter(SettingsPhone1View& v)
    : view(v)
{

}

void SettingsPhone1Presenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsPhone1Presenter::deactivate()
{

}

void SettingsPhone1Presenter::startPairing()
{
    if (!model->bleIsPairingActive()) {
        model->bleStartPairing();
    }
}
