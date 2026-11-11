#include <gui/userapp_screen/UserAppView.hpp>
#include <gui/userapp_screen/UserAppPresenter.hpp>

UserAppPresenter::UserAppPresenter(UserAppView& v)
    : view(v)
{

}

void UserAppPresenter::activate()
{
    uint32_t pid = model->appGetActivePID();
    if (pid != 0) {
        view.setApp(pid);
    }

    view.setAppSurface(model->getAppSurface());
}

void UserAppPresenter::deactivate()
{
    model->appPause();
}

void UserAppPresenter::onAppStartReq(uint32_t pid)
{
    view.setApp(pid);
    
    if (!model->appResume()) {
        model->switchToNextPriorityScreen();
    }
}

void UserAppPresenter::onAppStop()
{
    model->switchToNextPriorityScreen();
}
