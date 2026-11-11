#include <gui/usb_screen/USBView.hpp>
#include <gui/usb_screen/USBPresenter.hpp>

USBPresenter::USBPresenter(USBView& v)
    : view(v)
{

}

void USBPresenter::activate()
{

}

void USBPresenter::deactivate()
{

}

void USBPresenter::onUsbDisconnect()
{
    model->switchToNextPriorityScreen();
}

void USBPresenter::onUsbState(bool portOpen, bool charging)
{
    if (!portOpen) {
        model->application().gotoChargingScreenNoTransition();
    }
}