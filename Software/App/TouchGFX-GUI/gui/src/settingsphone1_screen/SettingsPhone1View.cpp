#include <gui/settingsphone1_screen/SettingsPhone1View.hpp>

SettingsPhone1View::SettingsPhone1View()
{

}

void SettingsPhone1View::setupScreen()
{
    SettingsPhone1ViewBase::setupScreen();

    title.set(T_TEXT_PHONE_UC);

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);
}

void SettingsPhone1View::tearDownScreen()
{
    SettingsPhone1ViewBase::tearDownScreen();
}

void SettingsPhone1View::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        presenter->startPairing();
        application().gotoPairingScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}

