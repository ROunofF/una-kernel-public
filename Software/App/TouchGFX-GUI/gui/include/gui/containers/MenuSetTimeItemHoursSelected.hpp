#ifndef MENUSETTIMEITEMHOURSSELECTED_HPP
#define MENUSETTIMEITEMHOURSSELECTED_HPP

#include <gui_generated/containers/MenuSetTimeItemHoursSelectedBase.hpp>

class MenuSetTimeItemHoursSelected : public MenuSetTimeItemHoursSelectedBase
{
public:
    MenuSetTimeItemHoursSelected();
    virtual ~MenuSetTimeItemHoursSelected() {}

    virtual void initialize();

    void setValue(int16_t v);
protected:
};

#endif // MENUSETTIMEITEMHOURSSELECTED_HPP
