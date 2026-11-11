#ifndef SETTINGSSATELLITESPRESENTER_HPP
#define SETTINGSSATELLITESPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsSatellitesView;

class SettingsSatellitesPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsSatellitesPresenter(SettingsSatellitesView& v);

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

    virtual ~SettingsSatellitesPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void saveGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode);

private:
    SettingsSatellitesPresenter();

    SettingsSatellitesView& view;
};

#endif // SETTINGSSATELLITESPRESENTER_HPP
