#ifndef PHONENOTIFICATIONVIEW_HPP
#define PHONENOTIFICATIONVIEW_HPP

#include <gui_generated/phonenotification_screen/PhoneNotificationViewBase.hpp>
#include <gui/phonenotification_screen/PhoneNotificationPresenter.hpp>

class PhoneNotificationView : public PhoneNotificationViewBase
{
public:
    PhoneNotificationView();
    virtual ~PhoneNotificationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setNotification(std::shared_ptr<PhoneNotification> notification);

    void removed(uint32_t uid, bool all);
    uint32_t getUid();
    bool isCall();

protected:
    static const uint16_t skTimeout = GUI_CONFIG_MS_2_TICKS(5 * 1000);
    uint16_t mCounter = skTimeout;
    std::shared_ptr<PhoneNotification> mNotification;

    static const uint16_t skIncominCallPeriod = GUI_CONFIG_MS_2_TICKS(2 * 1000);
    uint16_t mIncominCallCounter = skIncominCallPeriod;

    virtual void handleTickEvent() override;
    virtual void handleKeyEvent(uint8_t key) override;

    void setCategoryStyle(PhoneNotification::Category category);
    void fillActiveCallTime();
    bool isHigherPriority(PhoneNotification::Category n, PhoneNotification::Category o);

    
};

#endif // PHONENOTIFICATIONVIEW_HPP
