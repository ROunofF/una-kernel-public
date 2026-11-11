#include <gui/splash_screen/SplashView.hpp>
#include <gui/splash_screen/SplashPresenter.hpp>

SplashPresenter::SplashPresenter(SplashView& v)
    : view(v)
{

}

void SplashPresenter::activate()
{

}

void SplashPresenter::deactivate()
{

}

void SplashPresenter::onBooted()
{
    if (model->usbIsConnected()) {
        if (model->usbIsPortOpened()) {
            model->application().gotoUSBScreenNoTransition();
        } else {
            model->application().gotoChargingScreenNoTransition();
        }
    } else if (model->batteryIsLow()) {
        if (model->batteryIsShutdown()) {
            model->application().gotoLowBattShutDownScreenNoTransition();
        } else {
            model->application().gotoLowBatteryScreenNoTransition();
        }
    } else if (!model->bleIsBonded()) {
        model->application().gotoSplash1ScreenNoTransition();
    } else {
        model->application().gotoHomeScreenNoTransition();
    }
}