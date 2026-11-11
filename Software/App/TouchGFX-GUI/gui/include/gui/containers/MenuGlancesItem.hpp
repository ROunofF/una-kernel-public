#ifndef MENUGLANCESITEM_HPP
#define MENUGLANCESITEM_HPP

#include <gui_generated/containers/MenuGlancesItemBase.hpp>

class MenuGlancesItem : public MenuGlancesItemBase
{
public:
    MenuGlancesItem();
    virtual ~MenuGlancesItem() {}

    virtual void initialize();

    void setName(TypedTextId id);
    void setName(const char* name);

protected:
};

#endif // MENUGLANCESITEM_HPP
