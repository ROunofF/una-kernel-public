#include <gui/containers/MenuDevice.hpp>

MenuDevice::MenuDevice()
{

}

void MenuDevice::initialize()
{
    MenuDeviceBase::initialize();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);
        

    for (int i = 0; i < skMaxNumberOfItems; i++) {
        mItems[i].initialize();
        mItems[i].initialize();
    }
}

void MenuDevice::invalidate()
{
    for (int i = 0; i < list.getNumberOfItems(); i++) {
        list.itemChanged(i);
    }
    list.invalidate();
}

void MenuDevice::setNumberOfItems(int16_t numberOfItems)
{
    if (numberOfItems <= skMaxNumberOfItems) {
        list.setNumberOfItems(numberOfItems);
        sideBar.setCount(numberOfItems);
    }
}

int16_t MenuDevice::getNumberOfItems()
{
    return list.getNumberOfItems();
}

void MenuDevice::setTitle(TypedTextId msgId)
{
    title.set(msgId);
}

void MenuDevice::selectNext()
{
    if (!direction) {
        direction = true;
        p += (listListItems.getNumberOfDrawables() - 2);
    }
    p++;
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);

    if (p >= list.getNumberOfItems()) {
        p -= list.getNumberOfItems();
    }
    list.animateToItem(p, Gui::Config::kMenuAnimationSteps);
}

void MenuDevice::selectPrev()
{
    if (direction) {
        direction = false;
        p -= (listListItems.getNumberOfDrawables() - 2);
    }

    p--;
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);

    if (p < 0) {
        p += list.getNumberOfItems();
    }
    list.animateToItem(p, Gui::Config::kMenuAnimationSteps);
}

uint16_t MenuDevice::getSelectedItem()
{
    int16_t position = p - (direction ? 1 : 0);
    if (position < 0) {
        position += list.getNumberOfItems();
    }

    return position;
}

ScrollList &MenuDevice::getList()
{
    return list;
}

MenuDeviceItem *MenuDevice::getItem(int16_t itemIndex)
{
    if (itemIndex < skMaxNumberOfItems) {
        return &mItems[itemIndex];
    }

    return nullptr;
}

void MenuDevice::listUpdateItem(MenuDeviceItem &item, int16_t itemIndex)
{
    item = mItems[itemIndex];
}
