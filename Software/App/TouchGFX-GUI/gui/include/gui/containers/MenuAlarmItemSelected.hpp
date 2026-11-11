#ifndef MENUALARMITEMSELECTED_HPP
#define MENUALARMITEMSELECTED_HPP

#include <gui_generated/containers/MenuAlarmItemSelectedBase.hpp>

class MenuAlarmItemSelected : public MenuAlarmItemSelectedBase
{
public:
    MenuAlarmItemSelected();
    virtual ~MenuAlarmItemSelected() {}

    virtual void initialize();

    void config(TypedTextId msgId);

    void configToggle(TypedTextId msgIdLeft, TypedTextId msgIdRight);
    void setToggle(bool state);
    bool getToggle();

    MenuAlarmItemSelected &operator=(const MenuAlarmItemSelected &other);

protected:
    enum ActiveStyle {
        STYLE_SIMPLE = 0,   // Center
        STYLE_TOGGLE,       // Text + toggle + Text
    };

    ActiveStyle mStyle { };

    TypedTextId mMsgId = {TYPED_TEXT_INVALID};
    TypedTextId mMsgIdLeft = {TYPED_TEXT_INVALID};
    TypedTextId mMsgIdRight = {TYPED_TEXT_INVALID};

    void reset();
    void updStyle();
};

#endif // MENUALARMITEMSELECTED_HPP
