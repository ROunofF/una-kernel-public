#include <gui/charging_screen/ChargingView.hpp>

ChargingView::ChargingView()
{

}

void ChargingView::setupScreen()
{
    ChargingViewBase::setupScreen();
    progress.setColor(Gui::Config::Color::TEAL);
}

void ChargingView::tearDownScreen()
{
    ChargingViewBase::tearDownScreen();
}

void ChargingView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}

void ChargingView::setBatteryLevel(uint8_t level)
{
    if (level < 100) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(T_TEXT_CHARGING).getText());
    } else { 
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(T_TEXT_CHARGED).getText());
    }
    text.invalidate();

    Unicode::snprintf(valueBuffer, VALUE_SIZE, "%d%s", level, touchgfx::TypedText(T_TEXT_PERCENT).getText());
    value.invalidate();

    progress.setProgress(static_cast<float>(level));
}