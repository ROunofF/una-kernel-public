#ifndef USBPRESENTER_HPP
#define USBPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class USBView;

class USBPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    USBPresenter(USBView& v);

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

    virtual ~USBPresenter() {}

    virtual void onUsbDisconnect() override;
    virtual void onUsbState(bool portOpen, bool charging) override;
    virtual void onAppStartReq(uint32_t pid) override {}  // Not called in this screen
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

private:
    USBPresenter();

    USBView& view;
};

#endif // USBPRESENTER_HPP
