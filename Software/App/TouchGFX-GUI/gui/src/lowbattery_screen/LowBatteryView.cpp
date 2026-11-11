#include <gui/lowbattery_screen/LowBatteryView.hpp>

LowBatteryView::LowBatteryView()
{

}

void LowBatteryView::setupScreen()
{
    LowBatteryViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);
}

void LowBatteryView::tearDownScreen()
{
    LowBatteryViewBase::tearDownScreen();
}

void LowBatteryView::setBatteryLevel(uint8_t level)
{
    battery.setLevel(level);
}

void LowBatteryView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}

void LowBatteryView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        presenter->onTimeout();
    }
}
