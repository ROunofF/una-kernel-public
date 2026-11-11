#ifndef SHUTDOWNPRESENTER_HPP
#define SHUTDOWNPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ShutDownView;

class ShutDownPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ShutDownPresenter(ShutDownView& v);

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

    virtual ~ShutDownPresenter() {}

    virtual void onLowBattery() override { }


    // There should be no transitions from this screen.
    // Just wait a little and turn off the watch.
    virtual void onLowBatteryShutdown() override { }
    virtual void onBlePinCode() override { }
    virtual void onUsbConnect() override { }
    virtual void onFirmwareUpdStart() override { }
    virtual void onError() override { }   // terminal - do not jump to errors

    void shutdown();

private:
    ShutDownPresenter();

    ShutDownView& view;
};

#endif // SHUTDOWNPRESENTER_HPP
