#include <gui/pairing_screen/PairingView.hpp>

PairingView::PairingView()
{

}

void PairingView::setupScreen()
{
    PairingViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);
}

void PairingView::tearDownScreen()
{
    PairingViewBase::tearDownScreen();
}

void PairingView::setDeviceID(const char *idStr)
{
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(idStr), watchIdValueBuffer, WATCHIDVALUE_SIZE);

    watchIdValue.invalidate();
}

void PairingView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        application().gotoQRCodeScreenNoTransition();
    }
}

void PairingView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        application().gotoQRCodeScreenNoTransition();
    }
}