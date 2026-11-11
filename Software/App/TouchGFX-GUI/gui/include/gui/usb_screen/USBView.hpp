#ifndef USBVIEW_HPP
#define USBVIEW_HPP

#include <gui_generated/usb_screen/USBViewBase.hpp>
#include <gui/usb_screen/USBPresenter.hpp>

class USBView : public USBViewBase
{
public:
    USBView();
    virtual ~USBView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // USBVIEW_HPP
