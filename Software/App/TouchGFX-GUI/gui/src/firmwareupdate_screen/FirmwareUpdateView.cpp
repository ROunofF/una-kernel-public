#include <gui/firmwareupdate_screen/FirmwareUpdateView.hpp>

FirmwareUpdateView::FirmwareUpdateView()
{

}

void FirmwareUpdateView::setupScreen()
{
    FirmwareUpdateViewBase::setupScreen();
    progress.setColor(Gui::Config::Color::YELLOW_DARK);
}

void FirmwareUpdateView::tearDownScreen()
{
    FirmwareUpdateViewBase::tearDownScreen();
}

void  FirmwareUpdateView::setUpdatePct(uint8_t pct)
{
    if (pct < 100) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(T_TEXT_UPDATING).getText());
    } else {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(T_TEXT_UPDATED).getText());
    }
    text.invalidate();

    Unicode::snprintf(valueBuffer, VALUE_SIZE, "%d%s", pct, touchgfx::TypedText(T_TEXT_PERCENT).getText());
    value.invalidate();

    progress.setProgress(static_cast<float>(pct));
}