#ifndef HOMEPRESENTER_HPP
#define HOMEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class HomeView;

class HomePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    HomePresenter(HomeView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~HomePresenter() {}

    virtual void onCommonSettings(const WatchSettings &sett) override;
    virtual void onLocalSettings(const WatchSettingsLocal &sett) override;
    virtual void onBatteryLevel(uint8_t lvl) override;
    virtual void onTime(uint8_t h, uint8_t m, uint8_t s) override;
    virtual void onDate(uint8_t m, uint8_t d, uint8_t wd) override;

    virtual void onDailyHealth(uint32_t steps, uint32_t floors, uint32_t activityMinutes) override;

    virtual void onHomeWidget(const HomeWidgetData &widget) override;

    virtual void onWatchFaceRunning(uint32_t guiPid) override;

private:
    HomePresenter();

    HomeView& view;
};

#endif // HOMEPRESENTER_HPP
