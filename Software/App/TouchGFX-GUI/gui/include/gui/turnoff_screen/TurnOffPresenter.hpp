#ifndef TURNOFFPRESENTER_HPP
#define TURNOFFPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TurnOffView;

class TurnOffPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TurnOffPresenter(TurnOffView& v);

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

    virtual ~TurnOffPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

private:
    TurnOffPresenter();

    TurnOffView& view;
};

#endif // TURNOFFPRESENTER_HPP
