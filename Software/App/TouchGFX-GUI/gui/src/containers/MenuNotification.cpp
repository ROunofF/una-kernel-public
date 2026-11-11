#include <gui/containers/MenuNotification.hpp>

MenuNotification::MenuNotification() :
    mAnimationEndCb(this, &MenuNotification::scrollWheelAnimationEndCb)
{

}

void MenuNotification::initialize()
{
    MenuNotificationBase::initialize();
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);

    wheel.setAnimationEndedCallback(mAnimationEndCb);
}

int16_t MenuNotification::getNumberOfItems()
{
    return wheel.getNumberOfItems();
}

void MenuNotification::setBackground(Backgroung bckg)
{
    backgroungGreen.setVisible(false);
    backgroungGray.setVisible(false);

    switch (bckg) {
        case BACKGROUNG_GREEN:
            backgroungGreen.setVisible(true);
            break;
        case BACKGROUNG_GRAY:
            backgroungGray.setVisible(true);
            break;
        case BACKGROUNG_NONE:
        default:
            break;
    }

    backgroungGreen.invalidate();
    backgroungGray.invalidate();
}

void MenuNotification::setList(const std::vector<std::shared_ptr<PhoneNotification>> &list)
{
    mpList = &list;
    int16_t num = static_cast<int16_t>(mpList->size());

    if (num == 0) {
        wheel.setVisible(false);
        wheel.setNumberOfItems(0);
        sideBar.setCount(0);
        buttons.setR1(Buttons::NONE);
        setBackground(BACKGROUNG_NONE);
        textNoNotification.setVisible(true);
        sideBar.setVisible(false);
    } else { 
        wheel.setVisible(true);
        wheel.setNumberOfItems(num + 1);
        sideBar.setCount(num + 1);
        buttons.setR1(Buttons::AMBER);
        textNoNotification.setVisible(false);
        sideBar.setVisible(true);

        // Actual notification should be on second place and centered
        if (num == 1) {
            wheel.setCircular(false);
            sideBar.setCircular(false);
        } else {
            wheel.setCircular(true);
            sideBar.setCircular(true);
        }
        wheel.animateToItem(1, 0);
        sideBar.setActiveId(1);
        updateBackground(1);

        for (int i = 0; i < wheel.getNumberOfItems(); i++) {
            wheel.itemChanged(i);
        }
    }

    textNoNotification.invalidate();
    sideBar.invalidate();
    wheel.invalidate();
}

void MenuNotification::setTime(const std::tm &time)
{
    mTime = time;
}

void MenuNotification::selectNext()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() + 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);
    updateBackground(wheel.getSelectedItem());
}

void MenuNotification::selectPrev()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() - 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);
    updateBackground(wheel.getSelectedItem());
}

void MenuNotification::selectItem(int16_t itemIndex)
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    wheel.animateToItem(itemIndex, 0);
    sideBar.setActiveId(wheel.getSelectedItem());
}

void MenuNotification::selectItemUid(uint32_t uid)
{
    if (mpList == nullptr || uid == 0 ||  mpList->empty()) {
        return;
    }

    for (size_t i = 0; i < mpList->size(); i++) { 
        const std::shared_ptr<PhoneNotification> &notification = mpList->at(mpList->size() - i - 1);

        if (notification->getUid() == uid) {
            int16_t id = static_cast<int16_t>(i + 1);
            wheel.animateToItem(id, 0);
            sideBar.setActiveId(id + 1);
            updateBackground(id + 1);
            break;
        }
    }
}

uint16_t MenuNotification::getSelectedItem()
{
    return wheel.getSelectedItem();
}

uint32_t MenuNotification::getUidOfSelectedItem()
{
    int16_t i = wheel.getSelectedItem();

    if (mpList == nullptr || i < 1 || static_cast<size_t>(i) > mpList->size()) {
        return 0;
    }

    size_t id = i - 1;  // First item hold text, NOT a notification from list

    const std::shared_ptr<PhoneNotification> &notification = mpList->at(mpList->size() - id - 1);

    return notification->getUid();
}

void MenuNotification::wheelUpdateItem(MenuNotificationItemNotSelected &item, int16_t itemIndex)
{
    item.imageCallActiveSetVisible(false);
    item.imageCallMissedSetVisible(false);
    item.imageCallIncomingSetVisible(false);
    item.imageEmailSetVisible(false);
    item.imageMessageSetVisible(false);
    item.textClearAllSetVisible(false);

    if (itemIndex == 0) {
        item.textClearAllSetVisible(true);
        return;
    }

    if (mpList == nullptr || itemIndex < 1 || static_cast<size_t>(itemIndex) > mpList->size()) {
        return;
    }

    size_t id = itemIndex - 1;  // First item hold text, NOT a notification from list

    const std::shared_ptr<PhoneNotification> &notification = mpList->at(mpList->size() - id - 1);
    switch (notification->getCategory()) {
        case PhoneNotification::Category::ACTIVE_CALL:
            item.imageCallActiveSetVisible(true);
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            item.imageCallIncomingSetVisible(true);
            break;
        case PhoneNotification::Category::MISSED_CALL:
            item.imageCallMissedSetVisible(true);
            break;
        case PhoneNotification::Category::EMAIL:
            item.imageEmailSetVisible(false);
            break;
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            item.imageMessageSetVisible(true);
            break;
    }
    
}

void MenuNotification::wheelUpdateCenterItem(MenuNotificationItemSelected &item, int16_t itemIndex)
{
    // Hide all
    item.imageCallActiveSetVisible(false);
    item.imageCallMissedSetVisible(false);
    item.imageCallIncomingSetVisible(false);
    item.imageEmailSetVisible(false);
    item.imageMessageSetVisible(false);
    item.textClearAllSetVisible(false);
    item.textInfoSetVisible(false);

    if (itemIndex == 0) {
        item.textClearAllSetVisible(true);
        return;
    }

    if (mpList == nullptr || itemIndex < 1 || static_cast<size_t>(itemIndex) > mpList->size()) {
        return;
    }

    size_t id = itemIndex - 1;  // First item hold text, NOT a notification from list

    const std::shared_ptr<PhoneNotification> &notification = mpList->at(mpList->size() - id - 1);

    TypedTextId bodyId = TYPED_TEXT_INVALID;

    switch (notification->getCategory()) {
        case PhoneNotification::Category::ACTIVE_CALL:
            bodyId = T_TEXT_ACTIVE_CALL;
            item.imageCallActiveSetVisible(true);
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            bodyId = T_TEXT_INCOMING_CALL;
            item.imageCallIncomingSetVisible(true);
            break;
        case PhoneNotification::Category::MISSED_CALL:
            bodyId = T_TEXT_MISSED_CALL;
            item.imageCallMissedSetVisible(true);
            break;
        case PhoneNotification::Category::EMAIL:
            item.imageEmailSetVisible(false);
            break;
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            item.imageMessageSetVisible(true);
            break;
    }

    const uint16_t DAY_SIZE = 6;
    touchgfx::Unicode::UnicodeChar dayBuffer[DAY_SIZE] {};

    if (mTime.tm_year == notification->getTimestamp().tm_year && 
        mTime.tm_mon == notification->getTimestamp().tm_mon &&
        mTime.tm_mday == notification->getTimestamp().tm_mday) 
    {
        Unicode::snprintf(dayBuffer, DAY_SIZE, "%s", touchgfx::TypedText(T_TEXT_TODAY).getText());
    } else { 
        Unicode::snprintf(dayBuffer, DAY_SIZE, "%02u/%02u", notification->getTimestamp().tm_mon + 1, notification->getTimestamp().tm_mday);
    }
    
    const char *title = notification->getTitle().empty() ? nullptr : notification->getTitle().c_str();
    const char *body = notification->getSubTitle().empty() ?
        (notification->getMsg().empty() ? nullptr : notification->getMsg().c_str()) :
        notification->getSubTitle().c_str();

    if (bodyId != TYPED_TEXT_INVALID) {
        item.setContent(reinterpret_cast<const uint8_t *>(title), bodyId, dayBuffer, notification->getTimestamp().tm_hour, notification->getTimestamp().tm_min);
    } else { 
        item.setContent(reinterpret_cast<const uint8_t *>(title), reinterpret_cast<const uint8_t *>(body),
            dayBuffer, notification->getTimestamp().tm_hour, notification->getTimestamp().tm_min);
    }

    item.textInfoSetVisible(true);
}

void MenuNotification::updateBackground(int16_t p)
{
    if (p == 0) { 
        setBackground(BACKGROUNG_NONE);
    } else { 

        if (mpList == nullptr || p < 1 || static_cast<size_t>(p) > mpList->size()) {
            return;
        }

        size_t id = p - 1;  // First item hold text, NOT a notification from list

        const std::shared_ptr<PhoneNotification> &notification = mpList->at(mpList->size() - id - 1);

        setBackground(notification->isRead() ? BACKGROUNG_GRAY : BACKGROUNG_GREEN);
    }
}

void MenuNotification::scrollWheelAnimationEndCb()
{
    //updateBackground(wheel.getSelectedItem());
}
