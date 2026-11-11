#ifndef SETTINGSBACKLIGHTTIMEOUTPRESENTER_HPP
#define SETTINGSBACKLIGHTTIMEOUTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsBacklightTimeoutView;

class SettingsBacklightTimeoutPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsBacklightTimeoutPresenter(SettingsBacklightTimeoutView& v);

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

    virtual ~SettingsBacklightTimeoutPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void saveTimeout(uint8_t seconds);

private:
    SettingsBacklightTimeoutPresenter();

    SettingsBacklightTimeoutView& view;
};

#endif // SETTINGSBACKLIGHTTIMEOUTPRESENTER_HPP
