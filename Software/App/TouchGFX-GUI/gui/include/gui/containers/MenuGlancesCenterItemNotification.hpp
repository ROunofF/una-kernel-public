#ifndef MENUGLANCESCENTERITEMNOTIFICATION_HPP
#define MENUGLANCESCENTERITEMNOTIFICATION_HPP

#include <gui_generated/containers/MenuGlancesCenterItemNotificationBase.hpp>

class MenuGlancesCenterItemNotification : public MenuGlancesCenterItemNotificationBase
{
public:
    MenuGlancesCenterItemNotification();
    virtual ~MenuGlancesCenterItemNotification() {}

    virtual void initialize();

    void setCounters(uint32_t callsNum, uint32_t emailsNum, uint32_t msgNum);
protected:
};

#endif // MENUGLANCESCENTERITEMNOTIFICATION_HPP
