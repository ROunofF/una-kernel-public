#ifndef MENUDEVICEITEM_HPP
#define MENUDEVICEITEM_HPP

#include <gui_generated/containers/MenuDeviceItemBase.hpp>
#include <touchgfx/Unicode.hpp>

class MenuDeviceItem : public MenuDeviceItemBase
{
public:
    MenuDeviceItem();
    virtual ~MenuDeviceItem() {}

    virtual void initialize();

    void setTitle(TypedTextId msgId);
    void setValue(touchgfx::Unicode::UnicodeChar *msg);
    void setValue(const char *msg);

    MenuDeviceItem &operator=(const MenuDeviceItem &other);

protected:
};

#endif // MENUDEVICEITEM_HPP
