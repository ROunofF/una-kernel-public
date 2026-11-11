#include <gui/settingsphone2_screen/SettingsPhone2View.hpp>
#include <gui/settingsphone2_screen/SettingsPhone2Presenter.hpp>

SettingsPhone2Presenter::SettingsPhone2Presenter(SettingsPhone2View& v)
    : view(v)
{

}

void SettingsPhone2Presenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsPhone2Presenter::deactivate()
{

}

void SettingsPhone2Presenter::unpair()
{
    model->bleUnpair();
}