#ifndef MENUNOTIFICATIONITEMNOTSELECTED_HPP
#define MENUNOTIFICATIONITEMNOTSELECTED_HPP

#include <gui_generated/containers/MenuNotificationItemNotSelectedBase.hpp>

class MenuNotificationItemNotSelected : public MenuNotificationItemNotSelectedBase
{
public:
    MenuNotificationItemNotSelected();
    virtual ~MenuNotificationItemNotSelected() {}

    virtual void initialize();

    void imageCallActiveSetVisible(bool vis);
    void imageCallMissedSetVisible(bool vis);
    void imageCallIncomingSetVisible(bool vis);
    void imageEmailSetVisible(bool vis);
    void imageMessageSetVisible(bool vis);
    void textClearAllSetVisible(bool vis);

protected:
};

#endif // MENUNOTIFICATIONITEMNOTSELECTED_HPP
