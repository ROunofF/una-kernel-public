#include <gui/containers/MenuSetTimeItemMinutesNotSelected.hpp>

MenuSetTimeItemMinutesNotSelected::MenuSetTimeItemMinutesNotSelected()
{

}

void MenuSetTimeItemMinutesNotSelected::initialize()
{
    MenuSetTimeItemMinutesNotSelectedBase::initialize();
}

void MenuSetTimeItemMinutesNotSelected::setValue(int16_t v)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%02d", v);
}