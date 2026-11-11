#ifndef APPSTARTFAILEDVIEW_HPP
#define APPSTARTFAILEDVIEW_HPP

#include <gui_generated/appstartfailed_screen/AppStartFailedViewBase.hpp>
#include <gui/appstartfailed_screen/AppStartFailedPresenter.hpp>

/**
 * @brief Tells the user a launch they asked for did not happen.
 *
 *        Before this existed the launcher simply did nothing when an app
 *        failed to load - the only record was a log line no user ever sees.
 *        Out of memory is called out by name because it is the one
 *        cause the user has any chance of acting on, and the one that community
 *        apps are most likely to hit.
 *
 *        Dismisses itself, like SettingsFaceSaved: there is nothing to decide
 *        and nothing to confirm, so making the user press a button to get rid
 *        of it would only add a step.
 */
class AppStartFailedView : public AppStartFailedViewBase
{
public:
    AppStartFailedView();
    virtual ~AppStartFailedView() {}

    virtual void setupScreen();
    virtual void tearDownScreen();

    /**
     * @brief Choose the message. Called by the presenter on activate.
     */
    void setReason(App::LaunchFailure reason);

protected:
    virtual void handleTickEvent() override;

    uint16_t mCounter = Gui::Config::kAppStartFailedTimeout;
};

#endif // APPSTARTFAILEDVIEW_HPP
