#include <gui/bletesttxfcc_screen/BleTestTxFCCView.hpp>

BleTestTxFCCView::BleTestTxFCCView()
{
    buttons.setR1();
}

void BleTestTxFCCView::setupScreen()
{
    BleTestTxFCCViewBase::setupScreen();
}

void BleTestTxFCCView::tearDownScreen()
{
    BleTestTxFCCViewBase::tearDownScreen();
}

void BleTestTxFCCView::setChannel(const BleChannelsFCC::Info& ch)
{
    Unicode::snprintf(frequencyBuffer, FREQUENCY_SIZE, "%u MHz", ch.frequency);
    frequency.invalidate();
}

void BleTestTxFCCView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        application().gotoBleTestFCCScreenNoTransition();
    }
}
