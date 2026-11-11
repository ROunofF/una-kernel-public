#include <gui/splash1_screen/Splash1View.hpp>

Splash1View::Splash1View()
{

}

void Splash1View::setupScreen()
{
    Splash1ViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::NONE);
}

void Splash1View::tearDownScreen()
{
    Splash1ViewBase::tearDownScreen();
    
}

void Splash1View::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        application().gotoQRCodeScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {

    }
}