#include <gui/containers/MenuNotificationItemNotSelected.hpp>

MenuNotificationItemNotSelected::MenuNotificationItemNotSelected()
{

}

void MenuNotificationItemNotSelected::initialize()
{
    MenuNotificationItemNotSelectedBase::initialize();
}

void MenuNotificationItemNotSelected::imageCallActiveSetVisible(bool vis)
{
    imageCallActive.setVisible(vis);
    imageCallActive.invalidate();
}

void MenuNotificationItemNotSelected::imageCallMissedSetVisible(bool vis)
{
    imageCallMissed.setVisible(vis);
    imageCallMissed.invalidate();
}

void MenuNotificationItemNotSelected::imageCallIncomingSetVisible(bool vis)
{
    imageCallIncoming.setVisible(vis);
    imageCallIncoming.invalidate();
}

void MenuNotificationItemNotSelected::imageEmailSetVisible(bool vis)
{
    imageEmail.setVisible(vis);
    imageEmail.invalidate();
}

void MenuNotificationItemNotSelected::imageMessageSetVisible(bool vis)
{
    imageMessage.setVisible(vis);
    imageMessage.invalidate();
}

void MenuNotificationItemNotSelected::textClearAllSetVisible(bool vis)
{
    textClearAll.setVisible(vis);
    textClearAll.invalidate();
}