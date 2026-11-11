#ifndef LOWBATTSHUTDOWNPRESENTER_HPP
#define LOWBATTSHUTDOWNPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class LowBattShutDownView;

class LowBattShutDownPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    LowBattShutDownPresenter(LowBattShutDownView& v);

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

    virtual ~LowBattShutDownPresenter() {}

    // From this screen, there should be no transitions
    // other than connecting the USB and starting charging.
    virtual void onLowBatteryShutdown() override { }
    virtual void onBlePinCode() override { }
    virtual void onFirmwareUpdStart() override { }
    virtual void onError() override { }   // terminal - do not jump to errors

    // An app start answered while the countdown is running must not take the
    // screen either way. The countdown lives in the view, so leaving here
    // abandons it: the shutdown command is never sent, Power's timeout powers
    // off instead, and that path skips the daily-health flush in
    // guiHandler(Shutdown) - losing the in-progress day.
    virtual void onAppStartReq(uint32_t pid) override { }
    virtual void onAppLaunchFailed() override { }

    void shutdown();

private:
    LowBattShutDownPresenter();

    LowBattShutDownView& view;
};

#endif // LOWBATTSHUTDOWNPRESENTER_HPP
