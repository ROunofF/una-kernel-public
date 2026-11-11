#ifndef SETTINGSGNSSPRESENTER_HPP
#define SETTINGSGNSSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsGNSSView;

class SettingsGNSSPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsGNSSPresenter(SettingsGNSSView& v);

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

    virtual ~SettingsGNSSPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

private:
    SettingsGNSSPresenter();

    SettingsGNSSView& view;
};

#endif // SETTINGSGNSSPRESENTER_HPP
