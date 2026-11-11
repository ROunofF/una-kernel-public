#ifndef MENUSETTIMEITEMMINUTESSELECTED_HPP
#define MENUSETTIMEITEMMINUTESSELECTED_HPP

#include <gui_generated/containers/MenuSetTimeItemMinutesSelectedBase.hpp>

class MenuSetTimeItemMinutesSelected : public MenuSetTimeItemMinutesSelectedBase
{
public:
    MenuSetTimeItemMinutesSelected();
    virtual ~MenuSetTimeItemMinutesSelected() {}

    virtual void initialize();

    void setValue(int16_t v);
protected:
};

#endif // MENUSETTIMEITEMMINUTESSELECTED_HPP
