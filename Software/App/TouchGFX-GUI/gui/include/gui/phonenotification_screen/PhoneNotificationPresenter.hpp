#ifndef PHONENOTIFICATIONPRESENTER_HPP
#define PHONENOTIFICATIONPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PhoneNotificationView;

class PhoneNotificationPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PhoneNotificationPresenter(PhoneNotificationView& v);

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

    virtual ~PhoneNotificationPresenter() {}

    virtual void onUsbConnect() override { }    // Ignore USB

    virtual void onPhoneNotificationNew(std::shared_ptr<PhoneNotification> notification) override;
    virtual void onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list) override;
    virtual void onPhoneNotificationRemoved(uint32_t uid, bool all) override;

    void play();

    void stopPlay();

    void close();

    void setDetailsUid(uint32_t uid);

    void posAction(const std::shared_ptr<PhoneNotification> &notification);

    void negAction(const std::shared_ptr<PhoneNotification> &notification);

    uint32_t getTimePassMs(uint32_t notificationRxTimeMs);


private:
    PhoneNotificationPresenter();

    PhoneNotificationView& view;
    bool mIsCall = false;
};

#endif // PHONENOTIFICATIONPRESENTER_HPP
