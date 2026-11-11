#include <gui/containers/MenuSetTimeItemMinutesSelected.hpp>

MenuSetTimeItemMinutesSelected::MenuSetTimeItemMinutesSelected()
{

}

void MenuSetTimeItemMinutesSelected::initialize()
{
    MenuSetTimeItemMinutesSelectedBase::initialize();
}

void MenuSetTimeItemMinutesSelected::setValue(int16_t v)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%02d", v);
}