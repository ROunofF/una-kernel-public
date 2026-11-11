#include <gui/containers/MenuSetTimeItemHoursNotSelected.hpp>

MenuSetTimeItemHoursNotSelected::MenuSetTimeItemHoursNotSelected()
{

}

void MenuSetTimeItemHoursNotSelected::initialize()
{
    MenuSetTimeItemHoursNotSelectedBase::initialize();
}

void MenuSetTimeItemHoursNotSelected::setValue(int16_t v)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%02d", v);
}