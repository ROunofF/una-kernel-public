#ifndef MENUSETTIMEITEMHOURSNOTSELECTED_HPP
#define MENUSETTIMEITEMHOURSNOTSELECTED_HPP

#include <gui_generated/containers/MenuSetTimeItemHoursNotSelectedBase.hpp>

class MenuSetTimeItemHoursNotSelected : public MenuSetTimeItemHoursNotSelectedBase
{
public:
    MenuSetTimeItemHoursNotSelected();
    virtual ~MenuSetTimeItemHoursNotSelected() {}

    virtual void initialize();

    void setValue(int16_t v);
protected:
};

#endif // MENUSETTIMEITEMHOURSNOTSELECTED_HPP
