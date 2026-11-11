#ifndef PHONENOTIFICATIONLISTVIEW_HPP
#define PHONENOTIFICATIONLISTVIEW_HPP

#include <gui_generated/phonenotificationlist_screen/PhoneNotificationListViewBase.hpp>
#include <gui/phonenotificationlist_screen/PhoneNotificationListPresenter.hpp>

class PhoneNotificationListView : public PhoneNotificationListViewBase
{
public:
    PhoneNotificationListView();
    virtual ~PhoneNotificationListView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setTime(const std::tm &time);
    void setList(const std::vector<std::shared_ptr<PhoneNotification>> &list);

    void setPositionUid(uint32_t id);
    uint32_t getPositionUid();

protected:

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PHONENOTIFICATIONLISTVIEW_HPP
