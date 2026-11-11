#include <gui/settingsgnss_screen/SettingsGNSSView.hpp>

SettingsGNSSView::SettingsGNSSView()
{
    menu.setTitle(T_TEXT_GNSS);

    menu.setNumberOfItems(Gui::Config::Menu::Top::Settings::GNSS::ID_COUNT);

    update();
}

void SettingsGNSSView::setupScreen()
{
    SettingsGNSSViewBase::setupScreen();
}

void SettingsGNSSView::tearDownScreen()
{
    SettingsGNSSViewBase::tearDownScreen();
}

void SettingsGNSSView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t SettingsGNSSView::getPositionId()
{
    return menu.getSelectedItem();
}

void SettingsGNSSView::setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode)
{
    mMode = mode;
    update();
}

void SettingsGNSSView::handleKeyEvent(uint8_t key)
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
        case Gui::Config::Menu::Top::Settings::GNSS::ID_GPS_ASSIST:
            application().gotoSettingsGpsAssistScreenNoTransition();
            break;

        default:
            break;
        };
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}

void SettingsGNSSView::update()
{
    MenuItemSelected* pS = nullptr;
    MenuItemNotSelected* pN = nullptr;

    pS = menu.getSelectedItem(Gui::Config::Menu::Top::Settings::GNSS::ID_GPS_ASSIST);
    pS->config(T_TEXT_GPS_ASSIST_DOT);
    pN = menu.getNotSelectedItem(Gui::Config::Menu::Top::Settings::GNSS::ID_GPS_ASSIST);
    pN->config(T_TEXT_GPS_ASSIST_DOT);

    menu.invalidate();
}