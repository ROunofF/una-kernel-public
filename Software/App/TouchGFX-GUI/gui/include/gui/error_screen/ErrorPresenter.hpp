#ifndef ERRORPRESENTER_HPP
#define ERRORPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ErrorView;

class ErrorPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ErrorPresenter(ErrorView& v);

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

    virtual ~ErrorPresenter() {}

    // Already on the error screen: update the text for a new code, or leave
    // when there is nothing left to show.
    virtual void onError() override;

    // Ignore all events
    virtual void onLowBattery() override {}
    virtual void onBlePinCode() override {}
    virtual void onUsbConnect() override {}
    virtual void onFirmwareUpdStart() override {}
    virtual void onPhoneNotificationNew(std::shared_ptr<PhoneNotification> notification) override {}
    virtual void onAppStartReq(uint32_t pid) override {}
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

    void play();
    void silence();
    void exitScreen();
    void acknowledge();

private:
    ErrorPresenter();

    ErrorView& view;
};

#endif // ERRORPRESENTER_HPP
