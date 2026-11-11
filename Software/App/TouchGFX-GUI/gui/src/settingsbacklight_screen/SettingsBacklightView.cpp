#include <gui/settingsbacklight_screen/SettingsBacklightView.hpp>
#include <gui/common/BacklightLabels.hpp>

using BacklightId = Gui::Config::Menu::Top::Settings::Backlight::Id;

SettingsBacklightView::SettingsBacklightView()
{

}

void SettingsBacklightView::setupScreen()
{
    SettingsBacklightViewBase::setupScreen();

    menu.setTitle(T_TEXT_BACKLIGHT_UC);

    menu.setNumberOfItems(BacklightId::ID_COUNT);

    MenuItemSelected *pS = menu.getSelectedItem(BacklightId::ID_GESTURE);
    pS->configToggle(T_TEXT_GESTURE);
    pS->setMsgTypedTextId(T_TMP_SEMIBOLD_25);
    setGesture(mGesture);

    // The other entries carry their current value as a tip, so their items are
    // configured by setTimeout() / setSchedule(), which the presenter calls.
}

void SettingsBacklightView::tearDownScreen()
{
    SettingsBacklightViewBase::tearDownScreen();
}

void SettingsBacklightView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t SettingsBacklightView::getPositionId()
{
    return menu.getSelectedItem();
}

void SettingsBacklightView::setGesture(bool on)
{
    mGesture = on;

    menu.getSelectedItem(BacklightId::ID_GESTURE)->setToggle(on);

    MenuItemNotSelected *pN = menu.getNotSelectedItem(BacklightId::ID_GESTURE);
    pN->configTip(T_TEXT_GESTURE, on ? T_TEXT_ON_UC : T_TEXT_OFF_UC);
    pN->setTipColor(on ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON
                       : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsBacklightView::setTimeout(uint8_t seconds)
{
    const TypedTextId tipId = Gui::Labels::backlightTimeout(seconds);

    menu.getSelectedItem(BacklightId::ID_TIMEOUT)->configTip(T_TEXT_TIMEOUT, tipId);
    menu.getNotSelectedItem(BacklightId::ID_TIMEOUT)->configTip(T_TEXT_TIMEOUT, tipId);

    menu.invalidate();
}

void SettingsBacklightView::setSchedule(bool on)
{
    const TypedTextId tipId = on ? T_TEXT_ON_UC : T_TEXT_OFF_UC;

    menu.getSelectedItem(BacklightId::ID_SLEEP_MODE)->configTip(T_TEXT_SLEEP_MODE, tipId);

    MenuItemNotSelected *pN = menu.getNotSelectedItem(BacklightId::ID_SLEEP_MODE);
    pN->configTip(T_TEXT_SLEEP_MODE, tipId);
    pN->setTipColor(on ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON
                       : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsBacklightView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        switch (menu.getSelectedItem()) {
            case BacklightId::ID_GESTURE:
                setGesture(!mGesture);
                presenter->saveGesture(mGesture);
                break;
            case BacklightId::ID_TIMEOUT:
                application().gotoSettingsBacklightTimeoutScreenNoTransition();
                break;
            case BacklightId::ID_SLEEP_MODE:
                application().gotoSettingsSleepScreenNoTransition();
                break;
            default:
                break;
        }
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}
