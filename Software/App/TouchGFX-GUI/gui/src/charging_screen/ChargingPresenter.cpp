#include <gui/charging_screen/ChargingView.hpp>
#include <gui/charging_screen/ChargingPresenter.hpp>

ChargingPresenter::ChargingPresenter(ChargingView& v)
    : view(v)
{

}

void ChargingPresenter::activate()
{
    view.setBatteryLevel(model->batteryGetLevel());
}

void ChargingPresenter::deactivate()
{

}

void ChargingPresenter::onUsbDisconnect()
{
    model->switchToNextPriorityScreen();
}

void ChargingPresenter::onUsbState(bool portOpen, bool charging)
{ 
    if (portOpen) {
        model->application().gotoUSBScreenNoTransition();
    }
}

void ChargingPresenter::onBatteryLevel(uint8_t lvl)
{
    view.setBatteryLevel(lvl);
}

void ChargingPresenter::exit()
{
    if (model->chargingDismiss()) {
        model->switchToNextPriorityScreen();
    }
}
