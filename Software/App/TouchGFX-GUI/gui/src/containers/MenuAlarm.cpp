#include <gui/containers/MenuAlarm.hpp>

MenuAlarm::MenuAlarm()
{

}

void MenuAlarm::initialize()
{
    MenuAlarmBase::initialize();

    for (int i = 0; i < skMaxNumberOfItems; i++) {
        mSelItems[i].initialize();
        mNotSelItems[i].initialize();
    }

    
}

void MenuAlarm::invalidate()
{
    for (int i = 0; i < wheel.getNumberOfItems(); i++) {
        wheel.itemChanged(i);
    }
    wheel.invalidate();
}

void MenuAlarm::setNumberOfItems(int16_t numberOfItems)
{
    if (numberOfItems <= skMaxNumberOfItems) {
        wheel.setNumberOfItems(numberOfItems);
    }
}

int16_t MenuAlarm::getNumberOfItems()
{
    return wheel.getNumberOfItems();
}

void MenuAlarm::selectNext()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() + 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
}

void MenuAlarm::selectPrev()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() - 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
}

void MenuAlarm::selectItem(int16_t itemIndex)
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    wheel.animateToItem(itemIndex, 0);
}

uint16_t MenuAlarm::getSelectedItem()
{
    return wheel.getSelectedItem();
}

ScrollWheelWithSelectionStyle &MenuAlarm::getWheel()
{
    return wheel;
}

MenuAlarmItemSelected *MenuAlarm::getSelectedItem(int16_t itemIndex)
{
    if (itemIndex < skMaxNumberOfItems) {
        return &mSelItems[itemIndex];
    }

    return nullptr;
}

MenuAlarmItemNotSelected *MenuAlarm::getNotSelectedItem(int16_t itemIndex)
{
    if (itemIndex < skMaxNumberOfItems) {
        return &mNotSelItems[itemIndex];
    }

    return nullptr;
}


void MenuAlarm::wheelUpdateItem(MenuAlarmItemNotSelected &item, int16_t itemIndex)
{
    item = mNotSelItems[itemIndex];
}

void MenuAlarm::wheelUpdateCenterItem(MenuAlarmItemSelected &item, int16_t itemIndex)
{
    item = mSelItems[itemIndex];
}