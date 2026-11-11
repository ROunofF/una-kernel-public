#include <gui/settingssatellites_screen/SettingsSatellitesView.hpp>

SettingsSatellitesView::SettingsSatellitesView()
{

}

void SettingsSatellitesView::setupScreen()
{
    SettingsSatellitesViewBase::setupScreen();

    menu.setTitle(T_TEXT_MODE_UC);

    menu.setNumberOfItems(WatchSettingsLocal::GpsPowerMode::GPS_PM_COUNT);

    MenuItemSelected *pS = nullptr;
    MenuItemNotSelected *pN = nullptr;

    pS = menu.getSelectedItem(WatchSettingsLocal::GpsPowerMode::GPS_PM_STANDARD);
    pS->config(T_TEXT_STANDARD);
    pN = menu.getNotSelectedItem(WatchSettingsLocal::GpsPowerMode::GPS_PM_STANDARD);
    pN->config(T_TEXT_STANDARD);

    pS = menu.getSelectedItem(WatchSettingsLocal::GpsPowerMode::GPS_PM_PRECISION);
    pS->config(T_TEXT_PRECISION);
    pN = menu.getNotSelectedItem(WatchSettingsLocal::GpsPowerMode::GPS_PM_PRECISION);
    pN->config(T_TEXT_PRECISION);

    menu.invalidate();
}

void SettingsSatellitesView::tearDownScreen()
{
    SettingsSatellitesViewBase::tearDownScreen();
}

void SettingsSatellitesView::setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode)
{
    menu.selectItem(static_cast<int16_t>(mode));
}

void SettingsSatellitesView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        uint32_t id = menu.getSelectedItem();
        presenter->saveGpsPowerMode(static_cast<WatchSettingsLocal::GpsPowerMode>(id));
        application().gotoSettingsSatellitesSavedScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsGNSSScreenNoTransition();
    }
}
