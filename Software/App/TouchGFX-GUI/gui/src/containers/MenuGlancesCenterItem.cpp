#include <gui/containers/MenuGlancesCenterItem.hpp>

MenuGlancesCenterItem::MenuGlancesCenterItem()
{

}

void MenuGlancesCenterItem::initialize()
{
    MenuGlancesCenterItemBase::initialize();
}

void MenuGlancesCenterItem::setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgNum)
{
    if (!contains(notification)) {
        add(notification);
    }

    notification.setVisible(true);
    notification.setCounters(callsNum, emailsNum, msgNum);
    notification.invalidate();
}
