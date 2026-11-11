#ifndef PHONENOTIFICATIONDETAILSPRESENTER_HPP
#define PHONENOTIFICATIONDETAILSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PhoneNotificationDetailsView;

class PhoneNotificationDetailsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PhoneNotificationDetailsPresenter(PhoneNotificationDetailsView& v);

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

    virtual ~PhoneNotificationDetailsPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void negAction(const std::shared_ptr<PhoneNotification> &notification);

    void exit();

private:
    PhoneNotificationDetailsPresenter();

    PhoneNotificationDetailsView& view;
};

#endif // PHONENOTIFICATIONDETAILSPRESENTER_HPP
