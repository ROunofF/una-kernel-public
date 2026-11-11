#include <gui/settingsreset_screen/SettingsResetView.hpp>

SettingsResetView::SettingsResetView()
{

}

void SettingsResetView::setupScreen()
{
    SettingsResetViewBase::setupScreen();

    title.set(T_TEXT_SETTINGS_UC);
}

void SettingsResetView::tearDownScreen()
{
    SettingsResetViewBase::tearDownScreen();
}

void SettingsResetView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        presenter->reset();
        application().gotoSplashScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}
