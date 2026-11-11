#include <gui/settingsfacesaved_screen/SettingsFaceSavedView.hpp>

SettingsFaceSavedView::SettingsFaceSavedView()
{

}

void SettingsFaceSavedView::setupScreen()
{
    SettingsFaceSavedViewBase::setupScreen();

    title.set(T_TEXT_WATCH_FACE);
}

void SettingsFaceSavedView::tearDownScreen()
{
    SettingsFaceSavedViewBase::tearDownScreen();
}

void SettingsFaceSavedView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        application().gotoSettingsScreenNoTransition();
    }
}
