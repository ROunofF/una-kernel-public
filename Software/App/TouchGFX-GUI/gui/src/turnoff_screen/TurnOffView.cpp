#include <gui/turnoff_screen/TurnOffView.hpp>

TurnOffView::TurnOffView()
{

}

void TurnOffView::setupScreen()
{
    TurnOffViewBase::setupScreen();


    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);

    tick.setVisible(true);
    tick.invalidate();

    cross.setVisible(true);
    cross.invalidate();
}

void TurnOffView::tearDownScreen()
{
    TurnOffViewBase::tearDownScreen();
}

void TurnOffView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        application().gotoShutDownScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoTopMenuScreenNoTransition();
    }
}
