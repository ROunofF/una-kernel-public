#include <gui/containers/MenuSetTime.hpp>

MenuSetTime::MenuSetTime()
{

}

void MenuSetTime::initialize()
{
    MenuSetTimeBase::initialize();
}

void MenuSetTime::set12Hour(bool on)
{
    m12Hour = on;
    hoursWheel.setNumberOfItems(on ? 12 : 24);
    hoursWheel.invalidate();
}

void MenuSetTime::setTime(uint8_t h, uint8_t m)
{
    if (h < hoursWheel.getNumberOfItems() && m < 60) {
        hoursWheel.animateToItem(h, 0);
        setHours(h);

        minutesWheel.animateToItem(m, 0);
        setMinutes(m);
    }
}

void MenuSetTime::getTime(uint8_t &h, uint8_t &m)
{
    h = hoursWheel.getSelectedItem();
    m = minutesWheel.getSelectedItem();
}

void MenuSetTime::setActiveHours()
{
    mMinutesAactive = false;

    minutesWheel.setVisible(false);
    minutesWheel.invalidate();
    minutesInactive.setVisible(true);
    minutesInactive.invalidate();

    hoursWheel.setVisible(true);
    hoursWheel.invalidate();
    hoursInactive.setVisible(false);
    hoursInactive.invalidate();
}

void MenuSetTime::setActiveMinutes()
{
    mMinutesAactive = true;

    minutesWheel.setVisible(true);
    minutesWheel.invalidate();
    minutesInactive.setVisible(false);
    minutesInactive.invalidate();

    hoursWheel.setVisible(false);
    hoursWheel.invalidate();
    hoursInactive.setVisible(true);
    hoursInactive.invalidate();
}

void MenuSetTime::incValue()
{
    if (mMinutesAactive) {
        if (minutesWheel.getNumberOfItems() <= 1) {
            return;
        }

        int16_t p = minutesWheel.getSelectedItem() + 1;
        minutesWheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
        setMinutes(minutesWheel.getSelectedItem());
    } else {
        if (hoursWheel.getNumberOfItems() <= 1) {
            return;
        }
        int16_t p = hoursWheel.getSelectedItem() + 1;
        hoursWheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
        setHours(hoursWheel.getSelectedItem());
    }
}

void MenuSetTime::decValue()
{
    if (mMinutesAactive) {
        if (minutesWheel.getNumberOfItems() <= 1) {
            return;
        }

        int16_t p = minutesWheel.getSelectedItem() - 1;
        minutesWheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
        setMinutes(minutesWheel.getSelectedItem());
    } else {
        if (hoursWheel.getNumberOfItems() <= 1) {
            return;
        }

        int16_t p = hoursWheel.getSelectedItem() - 1;
        hoursWheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);

        setHours(hoursWheel.getSelectedItem());
    }
}

void MenuSetTime::hoursWheelUpdateItem(MenuSetTimeItemHoursNotSelected &item, int16_t itemIndex)
{
    item.setValue(displayHour(itemIndex));
}

void MenuSetTime::hoursWheelUpdateCenterItem(MenuSetTimeItemHoursSelected &item, int16_t itemIndex)
{
    item.setValue(displayHour(itemIndex));
}

void MenuSetTime::minutesWheelUpdateItem(MenuSetTimeItemMinutesNotSelected &item, int16_t itemIndex)
{
    item.setValue(itemIndex);
}

void MenuSetTime::minutesWheelUpdateCenterItem(MenuSetTimeItemMinutesSelected &item, int16_t itemIndex)
{
    item.setValue(itemIndex);
}

void MenuSetTime::setHours(int16_t h)
{
    Unicode::snprintf(hoursInactiveBuffer, HOURSINACTIVE_SIZE, "%02d", displayHour(h));
    hoursInactive.invalidate();
}

void MenuSetTime::setMinutes(int16_t m)
{
    Unicode::snprintf(minutesInactiveBuffer, MINUTESINACTIVE_SIZE, "%02d", m);
    minutesInactive.invalidate();
}