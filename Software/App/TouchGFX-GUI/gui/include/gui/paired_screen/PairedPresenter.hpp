#ifndef PAIREDPRESENTER_HPP
#define PAIREDPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PairedView;

class PairedPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PairedPresenter(PairedView& v);

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

    virtual ~PairedPresenter() {}

    virtual void onUsbConnect() override { model->application().gotoChargingScreenNoTransition(); }
    virtual void onLowBattery() override { model->application().gotoLowBatteryScreenNoTransition(); }
    virtual void onAppStartReq(uint32_t pid) override {}  // Not called in this screen
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

    void exit();
private:
    PairedPresenter();

    PairedView& view;
};

#endif // PAIREDPRESENTER_HPP
