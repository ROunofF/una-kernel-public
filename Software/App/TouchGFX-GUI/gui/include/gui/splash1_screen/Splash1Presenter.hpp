#ifndef SPLASH1PRESENTER_HPP
#define SPLASH1PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Splash1View;

class Splash1Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    Splash1Presenter(Splash1View& v);

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

    virtual ~Splash1Presenter() {}

private:
    Splash1Presenter();

    Splash1View& view;
};

#endif // SPLASH1PRESENTER_HPP
