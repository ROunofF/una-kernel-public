#ifndef MENUDEVICE_HPP
#define MENUDEVICE_HPP

#include <gui_generated/containers/MenuDeviceBase.hpp>

class MenuDevice : public MenuDeviceBase
{
public:
    MenuDevice();
    virtual ~MenuDevice() {}

    virtual void initialize();

    static const uint16_t skMaxNumberOfItems = 10;

    virtual void invalidate();

    void setNumberOfItems(int16_t numberOfItems);
    int16_t getNumberOfItems();

    void setTitle(TypedTextId msgId);

    void selectNext();
    void selectPrev();
    uint16_t getSelectedItem();

    ScrollList &getList();
    MenuDeviceItem *getItem(int16_t itemIndex);

protected:
    int16_t p {};
    bool direction {};
    MenuDeviceItem mItems[skMaxNumberOfItems] { };

    virtual void listUpdateItem(MenuDeviceItem &item, int16_t itemIndex) override;
};

#endif // MENUDEVICE_HPP
