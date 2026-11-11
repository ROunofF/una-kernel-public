#ifndef MENUGLANCESCENTERITEMLASTACTIVITY_HPP
#define MENUGLANCESCENTERITEMLASTACTIVITY_HPP

#include <gui_generated/containers/MenuGlancesCenterItemLastActivityBase.hpp>

class MenuGlancesCenterItemLastActivity : public MenuGlancesCenterItemLastActivityBase
{
public:
    MenuGlancesCenterItemLastActivity();
    virtual ~MenuGlancesCenterItemLastActivity();

    virtual void initialize();

    void setInfo(const uint8_t *pExtIcon, const std::tm *lastActivityTime);
protected:
    BitmapId mDynamicIconId = {BITMAP_INVALID};
};

#endif // MENUGLANCESCENTERITEMLASTACTIVITY_HPP
