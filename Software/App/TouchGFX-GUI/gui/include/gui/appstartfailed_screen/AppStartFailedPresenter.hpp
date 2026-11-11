#ifndef APPSTARTFAILEDPRESENTER_HPP
#define APPSTARTFAILEDPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class AppStartFailedView;

class AppStartFailedPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    AppStartFailedPresenter(AppStartFailedView& v);

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

    /**
     * @brief Leave for whichever menu the failed launch was started from.
     */
    void returnToLauncher();

    // Deliberately does NOT override onAppStartReq, unlike Charging, Error,
    // LowBattery and PairCode, which all suppress it. The inherited default
    // (appResume + go to the app) is the recovery for a confirmation that
    // arrives after this screen went up: the launch turned out to be merely
    // slow, so the app should open rather than the user being left looking
    // at a message saying it did not. Adding an empty override here to match
    // the other transient screens would break that silently.
    virtual ~AppStartFailedPresenter() {}

private:
    AppStartFailedPresenter();

    AppStartFailedView& view;
};

#endif // APPSTARTFAILEDPRESENTER_HPP
