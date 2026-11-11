#ifndef MENUSETTIMEITEMMINUTESNOTSELECTED_HPP
#define MENUSETTIMEITEMMINUTESNOTSELECTED_HPP

#include <gui_generated/containers/MenuSetTimeItemMinutesNotSelectedBase.hpp>

class MenuSetTimeItemMinutesNotSelected : public MenuSetTimeItemMinutesNotSelectedBase
{
public:
    MenuSetTimeItemMinutesNotSelected();
    virtual ~MenuSetTimeItemMinutesNotSelected() {}

    virtual void initialize();

    void setValue(int16_t v);
protected:
};

#endif // MENUSETTIMEITEMMINUTESNOTSELECTED_HPP
