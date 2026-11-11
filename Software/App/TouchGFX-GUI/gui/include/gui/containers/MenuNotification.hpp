#ifndef MENUNOTIFICATION_HPP
#define MENUNOTIFICATION_HPP

#include <gui_generated/containers/MenuNotificationBase.hpp>

class MenuNotification : public MenuNotificationBase
{
public:
    MenuNotification();
    virtual ~MenuNotification() {}

    virtual void initialize();

    int16_t getNumberOfItems();

    enum Backgroung {
        BACKGROUNG_NONE = 0,
        BACKGROUNG_GREEN,
        BACKGROUNG_GRAY,
    };
    void setBackground(Backgroung bckg);

    void setList(const std::vector<std::shared_ptr<PhoneNotification>> &list);
    void setTime(const std::tm &time);

    void selectNext();
    void selectPrev();
    void selectItem(int16_t itemIndex);
    void selectItemUid(uint32_t uid);
    uint16_t getSelectedItem();
    uint32_t getUidOfSelectedItem();


protected:
    const std::vector<std::shared_ptr<PhoneNotification>> *mpList = nullptr;
    std::tm mTime {};

    virtual void wheelUpdateItem(MenuNotificationItemNotSelected &item, int16_t itemIndex) override;
    virtual void wheelUpdateCenterItem(MenuNotificationItemSelected &item, int16_t itemIndex) override;

    void updateBackground(int16_t p);

    touchgfx::Callback <MenuNotification> mAnimationEndCb;
    void scrollWheelAnimationEndCb();


};

#endif // MENUNOTIFICATION_HPP
