#include <gui/containers/MenuSetTimeItemHoursSelected.hpp>

MenuSetTimeItemHoursSelected::MenuSetTimeItemHoursSelected()
{

}

void MenuSetTimeItemHoursSelected::initialize()
{
    MenuSetTimeItemHoursSelectedBase::initialize();
}

void MenuSetTimeItemHoursSelected::setValue(int16_t v)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%02d", v);
}