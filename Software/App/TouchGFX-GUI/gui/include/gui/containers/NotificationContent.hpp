#ifndef NOTIFICATIONCONTENT_HPP
#define NOTIFICATIONCONTENT_HPP

#include <gui_generated/containers/NotificationContentBase.hpp>

class NotificationContent : public NotificationContentBase
{
public:
    NotificationContent();
    virtual ~NotificationContent() {}

    virtual void initialize();

    void setContent(TypedTextId msgId);
    void setContent(const uint8_t *subtitle, const uint8_t *msg);

    int16_t getNumberOfItems();
    void selectNext();
    void selectPrev();
    void selectItem(int16_t itemIndex);
    int16_t getSelectedItem();


protected:
    int16_t mIndex = 0;
    int16_t mOrigPosition = 0;
};

#endif // NOTIFICATIONCONTENT_HPP
