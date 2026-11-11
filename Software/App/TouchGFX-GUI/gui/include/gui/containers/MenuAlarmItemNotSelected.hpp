#ifndef MENUALARMITEMNOTSELECTED_HPP
#define MENUALARMITEMNOTSELECTED_HPP

#include <gui_generated/containers/MenuAlarmItemNotSelectedBase.hpp>

class MenuAlarmItemNotSelected : public MenuAlarmItemNotSelectedBase
{
public:
    MenuAlarmItemNotSelected();
    virtual ~MenuAlarmItemNotSelected() {}

    virtual void initialize();

    void config(TypedTextId msgId);

    MenuAlarmItemNotSelected &operator=(const MenuAlarmItemNotSelected &other);

protected:

    enum ActiveStyle {
        STYLE_SIMPLE = 0,   // Center
    };

    ActiveStyle mStyle { };

    TypedTextId mMsgId = {TYPED_TEXT_INVALID};

    void reset();
    void updStyle();
};

#endif // MENUALARMITEMNOTSELECTED_HPP
