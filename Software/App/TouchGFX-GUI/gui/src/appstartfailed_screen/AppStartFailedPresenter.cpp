#include <gui/appstartfailed_screen/AppStartFailedPresenter.hpp>
#include <gui/appstartfailed_screen/AppStartFailedView.hpp>

AppStartFailedPresenter::AppStartFailedPresenter(AppStartFailedView& v)
    : view(v)
{

}

void AppStartFailedPresenter::activate()
{
    // Read on activate rather than being pushed the reason: the model records
    // it when the failure arrives and the screen change follows, so by the
    // time this runs the value is already there. That also means a screen
    // re-entered for any other reason still shows the right message.
    view.setReason(model->getAppLaunchFailure());
}

void AppStartFailedPresenter::deactivate()
{

}

void AppStartFailedPresenter::returnToLauncher()
{
    model->returnFromAppLaunchFailure();
}
