#ifndef PHONENOTIFICATIONCONFIRMPRESENTER_HPP
#define PHONENOTIFICATIONCONFIRMPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PhoneNotificationConfirmView;

class PhoneNotificationConfirmPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PhoneNotificationConfirmPresenter(PhoneNotificationConfirmView& v);

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

    virtual ~PhoneNotificationConfirmPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void clearAll();

private:
    PhoneNotificationConfirmPresenter();

    PhoneNotificationConfirmView& view;
};

#endif // PHONENOTIFICATIONCONFIRMPRESENTER_HPP
