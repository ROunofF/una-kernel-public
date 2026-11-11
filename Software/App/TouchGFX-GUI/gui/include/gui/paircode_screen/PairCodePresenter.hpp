#ifndef PAIRCODEPRESENTER_HPP
#define PAIRCODEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PairCodeView;

class PairCodePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PairCodePresenter(PairCodeView& v);

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

    virtual ~PairCodePresenter() {}

    virtual void onUsbConnect() override {}

    virtual void onBlePair(bool paired) override;
    virtual void onBleConnect(bool connected) override;

    virtual void onAppStartReq(uint32_t pid) override {}  // Not called in this screen
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

    void abortPairing();

private:
    PairCodePresenter();

    PairCodeView& view;
};

#endif // PAIRCODEPRESENTER_HPP
