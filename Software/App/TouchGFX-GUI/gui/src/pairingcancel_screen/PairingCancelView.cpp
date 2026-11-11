#include <gui/pairingcancel_screen/PairingCancelView.hpp>

PairingCancelView::PairingCancelView()
{
}

void PairingCancelView::setupScreen()
{
    PairingCancelViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);
}

void PairingCancelView::tearDownScreen()
{
    PairingCancelViewBase::tearDownScreen();
}

void PairingCancelView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
    }

    if (key == Gui::Config::Button::L2) {
    }

    if (key == Gui::Config::Button::R1) {
        presenter->abortPairing();
        application().gotoHomeScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoQRCodeScreenNoTransition();
    }
}
