#ifndef MENUALARM_HPP
#define MENUALARM_HPP

#include <gui_generated/containers/MenuAlarmBase.hpp>

class MenuAlarm : public MenuAlarmBase {
public:
    MenuAlarm();
    virtual ~MenuAlarm() { }

    virtual void initialize();
    virtual void invalidate();

    static const uint16_t skMaxNumberOfItems = 15;

    void setNumberOfItems(int16_t numberOfItems);
    int16_t getNumberOfItems();

    void selectNext();
    void selectPrev();
    void selectItem(int16_t itemIndex);
    uint16_t getSelectedItem();

    ScrollWheelWithSelectionStyle &getWheel();
    MenuAlarmItemSelected *getSelectedItem(int16_t itemIndex);
    MenuAlarmItemNotSelected *getNotSelectedItem(int16_t itemIndex);

protected:
    virtual void wheelUpdateItem(MenuAlarmItemNotSelected &item, int16_t itemIndex) override;
    virtual void wheelUpdateCenterItem(MenuAlarmItemSelected &item, int16_t itemIndex) override;

    MenuAlarmItemSelected mSelItems[skMaxNumberOfItems] { };
    MenuAlarmItemNotSelected mNotSelItems[skMaxNumberOfItems] { };
};

#endif // MENUALARM_HPP
