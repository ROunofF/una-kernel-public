#ifndef SETTINGSRESETPRESENTER_HPP
#define SETTINGSRESETPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsResetView;

class SettingsResetPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsResetPresenter(SettingsResetView& v);

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

    virtual ~SettingsResetPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void reset();

private:
    SettingsResetPresenter();

    SettingsResetView& view;
};

#endif // SETTINGSRESETPRESENTER_HPP
