#ifndef LOWBATTERYPRESENTER_HPP
#define LOWBATTERYPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class LowBatteryView;

class LowBatteryPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    LowBatteryPresenter(LowBatteryView& v);

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

    virtual ~LowBatteryPresenter() {}

    virtual void onBatteryLevel(uint8_t lvl) override;
    virtual void onUsbConnect() override;
    virtual void onAppStartReq(uint32_t pid) override {}  // Not called in this screen
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

    void exit();
    void onTimeout();

private:
    LowBatteryPresenter();

    LowBatteryView& view;
};

#endif // LOWBATTERYPRESENTER_HPP
