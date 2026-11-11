#ifndef WATCHFACE1_HPP
#define WATCHFACE1_HPP

#include <gui_generated/containers/WatchFace1Base.hpp>
#include "../../../../Libs/Header/Components/Shared/HomeWidget.hpp"

class WatchFace1 : public WatchFace1Base
{
public:
    WatchFace1();
    virtual ~WatchFace1() {}

    virtual void initialize();

    void setTopBarProgress(uint32_t c, uint32_t t);
    void setBotBarProgress(uint32_t c, uint32_t t);

    // A running app's pushed widget. While active it takes over the bottom bar
    // (its own percent + label); setBotBarProgress is ignored until it clears.
    void setHomeWidget(const HomeWidgetData &widget);

    void setMuteAlerts(bool muted);
    void setTime(uint8_t h, uint8_t m, uint8_t s);
    void setDate(uint8_t m, uint8_t d, uint8_t wd);
    void setClockFormat(WatchSettingsLocal::Clock::Format format);
    void setClockSeconds(bool state);
    void setClockDateOrder(WatchSettingsLocal::Clock::DateOrder order);
    void setBatteryLevel(uint8_t level);

protected:

    bool mWidgetActive = false;   ///< an app's widget currently owns the bottom bar

};

#endif // WATCHFACE1_HPP
