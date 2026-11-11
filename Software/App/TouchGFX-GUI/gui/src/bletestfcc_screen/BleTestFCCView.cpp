#include <gui/bletestfcc_screen/BleTestFCCView.hpp>

BleTestFCCView::BleTestFCCView()
{

}

void BleTestFCCView::setupScreen()
{
    BleTestFCCViewBase::setupScreen();

    menu.setTitle(T_TEXT_BLE_FCC_TEST);

    int16_t itemsNum = static_cast<int16_t>(BleChannelsFCC::list.size());
    menu.setNumberOfItems(itemsNum);

    for (int16_t i = 0; i < itemsNum; i++) {
        MenuItemSelected* pS = nullptr;
        MenuItemNotSelected* pN = nullptr;

        const uint16_t kBuffSize = 32;
        touchgfx::Unicode::UnicodeChar buffer[kBuffSize]{ };
        Unicode::snprintf(buffer, kBuffSize, "%u MHz", BleChannelsFCC::list.at(i).frequency);

        pS = menu.getSelectedItem(i);
        pS->config(buffer);

        pN = menu.getNotSelectedItem(i);
        pN->config(buffer);
    }

    menu.invalidate();
}

void BleTestFCCView::tearDownScreen()
{
    BleTestFCCViewBase::tearDownScreen();
}

void BleTestFCCView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t BleTestFCCView::getPositionId()
{
    return menu.getSelectedItem();
}

void BleTestFCCView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        application().gotoBleTestTxFCCScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}
