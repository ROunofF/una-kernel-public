#include <gui/containers/MenuNotificationItemSelected.hpp>

MenuNotificationItemSelected::MenuNotificationItemSelected()
{

}

void MenuNotificationItemSelected::initialize()
{
    MenuNotificationItemSelectedBase::initialize();
}

void MenuNotificationItemSelected::imageCallActiveSetVisible(bool vis)
{
    imageCallActive.setVisible(vis);
    imageCallActive.invalidate();
}

void MenuNotificationItemSelected::imageCallMissedSetVisible(bool vis)
{
    imageCallMissed.setVisible(vis);
    imageCallMissed.invalidate();
}

void MenuNotificationItemSelected::imageCallIncomingSetVisible(bool vis)
{
    imageCallIncoming.setVisible(vis);
    imageCallIncoming.invalidate();
}

void MenuNotificationItemSelected::imageEmailSetVisible(bool vis)
{
    imageEmail.setVisible(vis);
    imageEmail.invalidate();
}

void MenuNotificationItemSelected::imageMessageSetVisible(bool vis)
{
    imageMessage.setVisible(vis);
    imageMessage.invalidate();
}

void MenuNotificationItemSelected::textClearAllSetVisible(bool vis)
{
    textClearAll.setVisible(vis);
    textClearAll.invalidate();
}

void MenuNotificationItemSelected::textInfoSetVisible(bool vis)
{
    time.setVisible(vis);
    time.invalidate();

    day.setVisible(vis);
    day.invalidate();

    body.setVisible(vis);
    body.invalidate();

    title.setVisible(vis);
    title.invalidate();
}

void MenuNotificationItemSelected::setContent(const uint8_t *titleMsg, TypedTextId subtitleId, const touchgfx::Unicode::UnicodeChar *dayMsg, uint8_t h, uint8_t m)
{
    if (titleMsg) {
        Unicode::fromUTF8(titleMsg, titleBuffer, TITLE_SIZE);
        titleBuffer[TITLE_SIZE - 1] = 0; // ensure string termination
    } else {
        titleBuffer[0] = 0;
    }
    title.invalidate();

    Unicode::snprintf(bodyBuffer, BODY_SIZE, "%s", touchgfx::TypedText(subtitleId).getText());
    body.invalidate();

    Unicode::snprintf(dayBuffer, DAY_SIZE, "%s", dayMsg);
    day.invalidate();

    Unicode::snprintf(timeBuffer, TIME_SIZE, "%02u:%02u", h, m);
    time.invalidate();
}

void MenuNotificationItemSelected::setContent(const uint8_t *titleMsg, const uint8_t *subtitleMsg, const touchgfx::Unicode::UnicodeChar *dayMsg, uint8_t h, uint8_t m)
{
    if (titleMsg) {
        Unicode::fromUTF8(titleMsg, titleBuffer, TITLE_SIZE);
        titleBuffer[TITLE_SIZE - 1] = 0; // ensure string termination
    } else { 
        titleBuffer[0] = 0;
    }
    title.invalidate();

    if (subtitleMsg) {
        Unicode::fromUTF8(subtitleMsg, bodyBuffer, BODY_SIZE);
        bodyBuffer[BODY_SIZE - 1] = 0; // ensure string termination
    } else {
        bodyBuffer[0] = 0;
    }
    body.invalidate();

    Unicode::snprintf(dayBuffer, DAY_SIZE, "%s", dayMsg);
    day.invalidate();

    Unicode::snprintf(timeBuffer, TIME_SIZE, "%02u:%02u", h, m);
    time.invalidate();
}
