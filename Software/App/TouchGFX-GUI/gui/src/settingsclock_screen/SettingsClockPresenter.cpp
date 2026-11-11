#include <gui/settingsclock_screen/SettingsClockView.hpp>
#include <gui/settingsclock_screen/SettingsClockPresenter.hpp>

SettingsClockPresenter::SettingsClockPresenter(SettingsClockView& v)
    : view(v)
{

}

void SettingsClockPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuClock());

    // Reset idle timer
    model->resetIdleTimer();

    view.setFormat(model->getLocalSettings().clock.format);
    view.setDateOrder(model->getLocalSettings().clock.dateOrder);
}

void SettingsClockPresenter::deactivate()
{
    model->setPosMenuClock(view.getPositionId());
}
