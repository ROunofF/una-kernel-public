#include <gui/pairinginfo_screen/PairingInfoView.hpp>

PairingInfoView::PairingInfoView()
{

}

void PairingInfoView::setupScreen()
{
    PairingInfoViewBase::setupScreen();


    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);
}

void PairingInfoView::tearDownScreen()
{
    PairingInfoViewBase::tearDownScreen();
}


void PairingInfoView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        application().gotoPairingScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoQRCodeScreenNoTransition();
    }
}