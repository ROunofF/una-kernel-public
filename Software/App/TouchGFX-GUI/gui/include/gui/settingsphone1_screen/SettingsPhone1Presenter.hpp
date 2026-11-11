#ifndef SETTINGSPHONE1PRESENTER_HPP
#define SETTINGSPHONE1PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsPhone1View;

class SettingsPhone1Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsPhone1Presenter(SettingsPhone1View& v);

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

    virtual ~SettingsPhone1Presenter() {}

    void startPairing();

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

private:
    SettingsPhone1Presenter();

    SettingsPhone1View& view;
};

#endif // SETTINGSPHONE1PRESENTER_HPP
