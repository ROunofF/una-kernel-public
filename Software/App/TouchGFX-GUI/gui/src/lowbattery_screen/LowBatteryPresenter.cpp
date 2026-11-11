#include <gui/lowbattery_screen/LowBatteryView.hpp>
#include <gui/lowbattery_screen/LowBatteryPresenter.hpp>

LowBatteryPresenter::LowBatteryPresenter(LowBatteryView& v)
    : view(v)
{

}

void LowBatteryPresenter::activate()
{
    model->batteryPlayLow();
    view.setBatteryLevel(model->batteryGetLevel());
}

void LowBatteryPresenter::deactivate()
{

}

void LowBatteryPresenter::onBatteryLevel(uint8_t lvl)
{
    view.setBatteryLevel(lvl);
    if (!model->batteryIsLow()) { 
        model->switchToNextPriorityScreen();
    }
}

void LowBatteryPresenter::onUsbConnect()
{
    exit();
}

void LowBatteryPresenter::exit()
{
    model->batteryMuteLow();
    model->switchToNextPriorityScreen();
}

void LowBatteryPresenter::onTimeout()
{
    if (model->appGetActivePID() != 0) {
        exit();
    }
}