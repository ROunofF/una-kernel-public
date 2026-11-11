#include <gui/paircode_screen/PairCodeView.hpp>

PairCodeView::PairCodeView()
{

}

void PairCodeView::setupScreen()
{
    PairCodeViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);
}

void PairCodeView::tearDownScreen()
{
    PairCodeViewBase::tearDownScreen();
}

void PairCodeView::setCode(uint32_t pinCode)
{
    if (pinCode == 0 || pinCode > 999999) {
        Unicode::snprintf(codeBuffer, CODE_SIZE, "------");
    } else { 
        Unicode::snprintf(codeBuffer, CODE_SIZE, "%06u", pinCode);
    }
    code.invalidate();
}

void PairCodeView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        presenter->abortPairing();
    }
}

void PairCodeView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        presenter->abortPairing();
    }
}