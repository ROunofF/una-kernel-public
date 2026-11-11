#include <gui/settings_screen/SettingsView.hpp>

SettingsView::SettingsView()
{

}

void SettingsView::setupScreen()
{
    SettingsViewBase::setupScreen();

    menu.setTitle(T_TEXT_SETTINGS_UC);

    int16_t menuItemsCount = Gui::Config::Menu::Top::Settings::ID_COUNT;
    if (mUseBleFCC) {
        menuItemsCount += 1;
    }
    menu.setNumberOfItems(menuItemsCount);

    update();
}

void SettingsView::tearDownScreen()
{
    SettingsViewBase::tearDownScreen();
}

void SettingsView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t SettingsView::getPositionId()
{
    return menu.getSelectedItem();
}

void SettingsView::bonded(bool bonded)
{
    mBonded = bonded;
}

void SettingsView::useBleFCC(bool enable)
{
    mUseBleFCC = enable;

    int16_t menuItemsCount = Gui::Config::Menu::Top::Settings::ID_COUNT;
    if (mUseBleFCC) {
        menuItemsCount += 1;
    }
    menu.setNumberOfItems(menuItemsCount);
    update();
}

void SettingsView::handleKeyEvent(uint8_t key)
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
            case Gui::Config::Menu::Top::Settings::ID_CLOCK:
                application().gotoSettingsClockScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_WATCH_FACE:
                application().gotoSettingsWatchFaceScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_BACKLIGHT:
                application().gotoSettingsBacklightScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_PHONE:
                if (mBonded) {
                    application().gotoSettingsPhone2ScreenNoTransition();
                } else { 
                    application().gotoSettingsPhone1ScreenNoTransition();
                }
                break;
            case Gui::Config::Menu::Top::Settings::ID_SOUNDS:
                application().gotoSettingsSoundsScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_VIBRATION:
                application().gotoSettingsVibrationScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_GNSS:
                application().gotoSettingsGNSSScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_DEVICE:
                application().gotoSettingsDeviceScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_SENSORS:
                application().gotoSettingsSensorsScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::Settings::ID_RESET:
                application().gotoSettingsResetScreenNoTransition();
                break;
            case (Gui::Config::Menu::Top::Settings::ID_COUNT + 0):
                if (mUseBleFCC) {
                    application().gotoBleTestFCCScreenNoTransition();
                }
                break;
            default:
                break;
        };
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoTopMenuScreenNoTransition();
    }
}

void SettingsView::update()
{

    MenuItemSelected *pS = nullptr;
    MenuItemNotSelected *pN = nullptr;

    // CLOCK
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_CLOCK);
    pS->config(T_TEXT_CLOCK);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_CLOCK);
    pN->config(T_TEXT_CLOCK);

    // WATCH FACE
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_WATCH_FACE);
    pS->config(T_TEXT_WATCH_FACE);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_WATCH_FACE);
    pN->config(T_TEXT_WATCH_FACE);

    // BACKLIGHT
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_BACKLIGHT);
    pS->config(T_TEXT_BACKLIGHT);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_BACKLIGHT);
    pN->config(T_TEXT_BACKLIGHT);

    // PHONE
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_PHONE);
    pS->config(T_TEXT_PHONE);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_PHONE);
    pN->config(T_TEXT_PHONE);

    // SOUNDS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_SOUNDS);
    pS->config(T_TEXT_SOUNDS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_SOUNDS);
    pN->config(T_TEXT_SOUNDS);

    // VIBRATION
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_VIBRATION);
    pS->config(T_TEXT_VIBRATION);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_VIBRATION);
    pN->config(T_TEXT_VIBRATION);

    // GNSS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_GNSS);
    pS->config(T_TEXT_GNSS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_GNSS);
    pN->config(T_TEXT_GNSS);

    // DEVICE
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_DEVICE);
    pS->config(T_TEXT_DEVICE);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_DEVICE);
    pN->config(T_TEXT_DEVICE);

    // SENSORS
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_SENSORS);
    pS->config(T_TEXT_SENSORS);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_SENSORS);
    pN->config(T_TEXT_SENSORS);

    // RESET
    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_RESET);
    pS->config(T_TEXT_RESET);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_RESET);
    pN->config(T_TEXT_RESET);

    if (mUseBleFCC) {
        pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::ID_COUNT);
        pS->config(T_TEXT_BLE_FCC_TEST);
        pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::ID_COUNT);
        pN->config(T_TEXT_BLE_FCC_TEST);
    }

    menu.invalidate();
}
