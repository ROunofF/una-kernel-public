#include <gui/containers/MenuGlancesCenterItemNotification.hpp>

MenuGlancesCenterItemNotification::MenuGlancesCenterItemNotification()
{

}

void MenuGlancesCenterItemNotification::initialize()
{
    MenuGlancesCenterItemNotificationBase::initialize();
}

void MenuGlancesCenterItemNotification::setCounters(uint32_t callsNum, uint32_t emailsNum, uint32_t msgNum)
{
    Unicode::snprintf(valueCallBuffer, VALUECALL_SIZE, "%u", callsNum < 99 ? callsNum : 99);
    valueCall.invalidate();
    Unicode::snprintf(valueEmailBuffer, VALUEEMAIL_SIZE, "%u", emailsNum < 99 ? emailsNum : 99);
    valueEmail.invalidate();
    Unicode::snprintf(valueMessageBuffer, VALUEMESSAGE_SIZE, "%u", msgNum < 99 ? msgNum : 99);
    valueMessage.invalidate();
}