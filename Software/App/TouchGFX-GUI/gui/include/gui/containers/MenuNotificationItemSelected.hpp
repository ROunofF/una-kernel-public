#ifndef MENUNOTIFICATIONITEMSELECTED_HPP
#define MENUNOTIFICATIONITEMSELECTED_HPP

#include <gui_generated/containers/MenuNotificationItemSelectedBase.hpp>

class MenuNotificationItemSelected : public MenuNotificationItemSelectedBase
{
public:
    MenuNotificationItemSelected();
    virtual ~MenuNotificationItemSelected() {}

    virtual void initialize();

    void imageCallActiveSetVisible(bool vis);
    void imageCallMissedSetVisible(bool vis);
    void imageCallIncomingSetVisible(bool vis);
    void imageEmailSetVisible(bool vis);
    void imageMessageSetVisible(bool vis);
    void textClearAllSetVisible(bool vis);

    void textInfoSetVisible(bool vis);

    void setContent(const uint8_t *titleMsg, TypedTextId subtitleId, const touchgfx::Unicode::UnicodeChar *dayMsg, uint8_t h, uint8_t m);
    void setContent(const uint8_t *titleMsg, const uint8_t *subtitleMsg, const touchgfx::Unicode::UnicodeChar *dayMsg, uint8_t h, uint8_t m);

protected:
};

#endif // MENUNOTIFICATIONITEMSELECTED_HPP
