#include <gui/error_screen/ErrorView.hpp>

ErrorView::ErrorView()
{
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::NONE);
}

void ErrorView::setupScreen()
{
    ErrorViewBase::setupScreen();

    // The base has just applied the designer's layout; remember where the name
    // field is centred before setError() starts resizing it to its text.
    mNameCentreY = name.getY() + name.getHeight() / 2;
}

void ErrorView::tearDownScreen()
{
    ErrorViewBase::tearDownScreen();
}

void ErrorView::setError(SystemError e)
{
    mMuted = false;   // a (re)shown error re-arms the alert

    // Each error maps to a title (top, multi-line) and a short tag (bottom, red).
    touchgfx::TypedText title(T_TEXT_ERROR_TITLE_UNKNOWN);
    touchgfx::TypedText tag(T_TEXT_ERROR_TAG_UNKNOWN);

    switch (e) {
    case SystemError::CABLE_CONNECTION:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_CABLE_CONN);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_CABLE_CONN);
        break;
    case SystemError::SYS_VOLTAGE:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_SYS_VOLTAGE);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_SYS_VOLTAGE);
        break;
    case SystemError::USB_PROTECTION:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_USB_PROTECTION);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_USB_PROTECTION);
        break;
    case SystemError::TEMPERATURE_AMBIENT:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_TEMP_AMBIENT);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_TEMP_AMBIENT);
        break;
    case SystemError::TEMPERATURE_CABLE:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_TEMP_CABLE);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_TEMP_CABLE);
        break;
    case SystemError::TEMPERATURE_CRITICAL:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_TEMP_CRITICAL);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_TEMP_CRITICAL);
        break;
    case SystemError::TEMPERATURE_SHUTDOWN:
        title = touchgfx::TypedText(T_TEXT_ERROR_TITLE_TEMP_SHUTDOWN);
        tag   = touchgfx::TypedText(T_TEXT_ERROR_TAG_TEMP_SHUTDOWN);
        break;
    default:
        break;   // no mapping - fall back to the unknown text
    }

    // name (top): fill the wildcard with the title (multi-line), trim its
    // height to the actual line count, then recentre it on the line the
    // designer placed it on, so one, two and three line titles all sit level.
    name.invalidate();
    Unicode::snprintf(nameBuffer, NAME_SIZE, "%s", title.getText());
    name.resizeHeightToCurrentText();
    name.setY(mNameCentreY - name.getHeight() / 2);
    name.invalidate();

    // errType (bottom, red): the short tag, wrapped in parentheses.
    Unicode::snprintf(errTypeBuffer, ERRTYPE_SIZE, "(%s)", tag.getText());
    errType.invalidate();
}

void ErrorView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        // R1 always silences the buzzer/vibro locally, and asks to acknowledge.
        // The arbiter decides the outcome: a dismissable error is hidden (the
        // backend then leaves the screen), a non-dismissable one (over-temp
        // "disconnect cable" / "turning off") stays - so there R1 only silences.
        mMuted = true;
        presenter->silence();
        presenter->acknowledge();
    }

    if (key == Gui::Config::Button::R2) {

    }
}

void ErrorView::handleTickEvent()
{
    if (!mMuted && mRepeatPeriod >= Gui::Config::kErrorPlayPeriod) {
        presenter->play();
        mRepeatPeriod = 0;
    }
    mRepeatPeriod++;
}
