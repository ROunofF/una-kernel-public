#ifndef PHONENOTIFICATIONLISTPRESENTER_HPP
#define PHONENOTIFICATIONLISTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PhoneNotificationListView;

class PhoneNotificationListPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PhoneNotificationListPresenter(PhoneNotificationListView& v);

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

    virtual ~PhoneNotificationListPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }
    virtual void onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list) override;

private:
    PhoneNotificationListPresenter();

    PhoneNotificationListView& view;
};

#endif // PHONENOTIFICATIONLISTPRESENTER_HPP
