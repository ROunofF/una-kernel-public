#ifndef PHONENOTIFICATIONDETAILSVIEW_HPP
#define PHONENOTIFICATIONDETAILSVIEW_HPP

#include <gui_generated/phonenotificationdetails_screen/PhoneNotificationDetailsViewBase.hpp>
#include <gui/phonenotificationdetails_screen/PhoneNotificationDetailsPresenter.hpp>

class PhoneNotificationDetailsView : public PhoneNotificationDetailsViewBase
{
public:
    PhoneNotificationDetailsView();
    virtual ~PhoneNotificationDetailsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setNotification(std::shared_ptr<PhoneNotification> notification);

protected:
    std::shared_ptr<PhoneNotification> mNotification;
    int16_t mScrollSteps = 0;

    virtual void handleTickEvent() override;
    virtual void handleKeyEvent(uint8_t key) override;

    void setCategoryStyle(PhoneNotification::Category category);
    void updateScroll();
};

#endif // PHONENOTIFICATIONDETAILSVIEW_HPP
