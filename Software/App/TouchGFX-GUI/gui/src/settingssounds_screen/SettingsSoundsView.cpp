#include <gui/settingssounds_screen/SettingsSoundsView.hpp>

SettingsSoundsView::SettingsSoundsView()
{

}

void SettingsSoundsView::setupScreen()
{
    SettingsSoundsViewBase::setupScreen();

    menu.setTitle(T_TEXT_SOUNDS_UC);

    menu.setNumberOfItems(Gui::Config::Menu::Top::Settings::Sounds::ID_COUNT);

    MenuItemSelected *pS = nullptr;

    // BUTTONS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_BUTTONS);
    pS->configToggle(T_TEXT_BUTTONS);
    pS->setMsgTypedTextId(T_TMP_SEMIBOLD_25);
    setButtons(mButtons);

    // MSG_AND_CALLS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_MSG_AND_CALLS);
    pS->configToggle(T_TEXT_MSG_DOT_AND_BR_CALLS);
    pS->setMsgTypedTextId(T_TMP_SEMIBOLD_25);
    setMsgCalls(mMsgCalls);

    // ALERTS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_ALERTS);
    pS->configToggle(T_TEXT_ALERTS);
    pS->setMsgTypedTextId(T_TMP_SEMIBOLD_25);
    setAlerts(mAlerts);

    menu.invalidate();
}

void SettingsSoundsView::tearDownScreen()
{
    SettingsSoundsViewBase::tearDownScreen();
}

void SettingsSoundsView::setButtons(bool state)
{
    mButtons = state;

    MenuItemSelected *pS = nullptr;
    MenuItemNotSelected *pN = nullptr;
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_BUTTONS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_BUTTONS);

    pS->setToggle(mButtons);

    pN->configTip(T_TEXT_BUTTONS, state ? T_TEXT_ON_UC : T_TEXT_OFF_UC);
    pN->setTipColor(state ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsSoundsView::setMsgCalls(bool state)
{
    mMsgCalls = state;

    MenuItemSelected *pS = nullptr;
    MenuItemNotSelected *pN = nullptr;
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_MSG_AND_CALLS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_MSG_AND_CALLS);

    pS->setToggle(mMsgCalls);

    pN->configTip(T_TEXT_MSG_DOT_AND_CALLS, state ? T_TEXT_ON_UC : T_TEXT_OFF_UC);
    pN->setTipColor(state ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsSoundsView::setAlerts(bool state)
{
    mAlerts = state;

    MenuItemSelected *pS = nullptr;
    MenuItemNotSelected *pN = nullptr;
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_ALERTS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::Sounds::ID_ALERTS);

    pS->setToggle(mAlerts);

    pN->configTip(T_TEXT_ALERTS, state ? T_TEXT_ON_UC : T_TEXT_OFF_UC);
    pN->setTipColor(state ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsSoundsView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        uint32_t id = menu.getSelectedItem();

        switch (id) {
            case Gui::Config::Menu::Top::Settings::Sounds::ID_BUTTONS:
                mButtons = !mButtons;
                setButtons(mButtons);
                presenter->saveButtons(mButtons);
                break;
            case Gui::Config::Menu::Top::Settings::Sounds::ID_MSG_AND_CALLS:
                mMsgCalls = !mMsgCalls;
                setMsgCalls(mMsgCalls);
                presenter->saveMsgCalls(mMsgCalls);
                break;
            case Gui::Config::Menu::Top::Settings::Sounds::ID_ALERTS:
                mAlerts = !mAlerts;
                setAlerts(mAlerts);
                presenter->saveAlerts(mAlerts);
                break;
            default:
                break;
        };
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}