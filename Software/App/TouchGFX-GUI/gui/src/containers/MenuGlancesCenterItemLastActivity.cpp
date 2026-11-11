#include <gui/containers/MenuGlancesCenterItemLastActivity.hpp>

MenuGlancesCenterItemLastActivity::MenuGlancesCenterItemLastActivity()
{

}

MenuGlancesCenterItemLastActivity::~MenuGlancesCenterItemLastActivity()
{
    // Delete previous bitmap
    if (mDynamicIconId != BITMAP_INVALID) {
        Bitmap::dynamicBitmapDelete(mDynamicIconId);
    }
}

void MenuGlancesCenterItemLastActivity::initialize()
{
    MenuGlancesCenterItemLastActivityBase::initialize();
}

void MenuGlancesCenterItemLastActivity::setInfo(const uint8_t *pExtIcon, const std::tm *lastActivityTime)
{
    if (pExtIcon != nullptr) {
        // Delete previous bitmap
        if (mDynamicIconId != BITMAP_INVALID) {
            Bitmap::dynamicBitmapDelete(mDynamicIconId);
        }
        // Create (16bit) dynamic bitmap
        mDynamicIconId = Bitmap::dynamicBitmapCreateExternal(App::kIconWH, App::kIconWH, pExtIcon, Bitmap::ABGR2222);
        icon.setBitmap(Bitmap(mDynamicIconId));
        icon.setVisible(true);
    } else { 
        icon.setVisible(false);
    }
    icon.invalidate();

    // No saved activity
    if (lastActivityTime == nullptr || (lastActivityTime->tm_year == 0 && lastActivityTime->tm_mon == 0 && lastActivityTime->tm_mday == 0)) {
        if (pExtIcon != nullptr) {
            Unicode::snprintf(textDateBuffer, TEXTDATE_SIZE, "%s", touchgfx::TypedText(T_TEXT_NO_ACTIVITY).getText());
        } else { 
            Unicode::snprintf(textDateBuffer, TEXTDATE_SIZE, "%s", touchgfx::TypedText(T_TEXT_NO_SAVED_ACTIVITIES).getText());
        }
    } else { 
        Unicode::snprintf(textDateBuffer, TEXTDATE_SIZE, "%s %02u, %02u:%02u", 
            touchgfx::TypedText(Gui::Config::DateTime::DayShort[lastActivityTime->tm_wday]).getText(),
            lastActivityTime->tm_mday, lastActivityTime->tm_hour, lastActivityTime->tm_min);
    }
    textDate.invalidate();
}
