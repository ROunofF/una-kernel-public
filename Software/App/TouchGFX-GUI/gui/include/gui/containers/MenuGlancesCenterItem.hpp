#ifndef MENUGLANCESCENTERITEM_HPP
#define MENUGLANCESCENTERITEM_HPP

#include <gui_generated/containers/MenuGlancesCenterItemBase.hpp>

class MenuGlancesCenterItem : public MenuGlancesCenterItemBase
{
public:
    MenuGlancesCenterItem();
    virtual ~MenuGlancesCenterItem() {}

    virtual void initialize();

    // Drawable cannot be added to two containers at the same time.
    // When there are no other items in the menu except Notifications,
    // the Drawable Notifications must be added to two items at the
    // same time, so we add it to each CenterItem instead of setting
    // it externally.
    void setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgNum);

protected:

};

#endif // MENUGLANCESCENTERITEM_HPP
