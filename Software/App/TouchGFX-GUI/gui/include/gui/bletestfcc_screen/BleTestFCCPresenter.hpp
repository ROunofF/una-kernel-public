#ifndef BLETESTFCCPRESENTER_HPP
#define BLETESTFCCPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class BleTestFCCView;

class BleTestFCCPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    BleTestFCCPresenter(BleTestFCCView& v);

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

    virtual ~BleTestFCCPresenter() {}

    void runTest(BleChannelsFCC::Info ch);

private:
    BleTestFCCPresenter();

    BleTestFCCView& view;
};

#endif // BLETESTFCCPRESENTER_HPP
