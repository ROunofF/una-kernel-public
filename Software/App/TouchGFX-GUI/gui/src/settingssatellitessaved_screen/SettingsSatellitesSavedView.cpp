#include <gui/settingssatellitessaved_screen/SettingsSatellitesSavedView.hpp>

SettingsSatellitesSavedView::SettingsSatellitesSavedView()
{

}

void SettingsSatellitesSavedView::setupScreen()
{
    SettingsSatellitesSavedViewBase::setupScreen();

    title.set(T_TEXT_MODE_UC);
}

void SettingsSatellitesSavedView::tearDownScreen()
{
    SettingsSatellitesSavedViewBase::tearDownScreen();
}

void SettingsSatellitesSavedView::setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode)
{
    if (mode == WatchSettingsLocal::GpsPowerMode::GPS_PM_STANDARD) {
        Unicode::snprintf(textModeBuffer, TEXTMODE_SIZE, "%s", touchgfx::TypedText(T_TEXT_STANDARD).getText());
    } else if (mode == WatchSettingsLocal::GpsPowerMode::GPS_PM_PRECISION) {
        Unicode::snprintf(textModeBuffer, TEXTMODE_SIZE, "%s", touchgfx::TypedText(T_TEXT_PRECISION).getText());
    }
    textMode.invalidate();
}

void SettingsSatellitesSavedView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        application().gotoSettingsScreenNoTransition();
    }
}
