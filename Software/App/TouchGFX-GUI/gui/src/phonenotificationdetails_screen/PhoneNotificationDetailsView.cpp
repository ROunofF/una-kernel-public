#include <gui/phonenotificationdetails_screen/PhoneNotificationDetailsView.hpp>

PhoneNotificationDetailsView::PhoneNotificationDetailsView()
{

}

void PhoneNotificationDetailsView::setupScreen()
{
    PhoneNotificationDetailsViewBase::setupScreen();
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::NONE);
}

void PhoneNotificationDetailsView::tearDownScreen()
{
    PhoneNotificationDetailsViewBase::tearDownScreen();
}

void PhoneNotificationDetailsView::setNotification(std::shared_ptr<PhoneNotification> notification)
{
    if (notification == nullptr) {
        return;
    }

    mNotification = notification;

    setCategoryStyle(mNotification->getCategory());

    Unicode::fromUTF8(reinterpret_cast<const uint8_t *>(mNotification->getTitle().c_str()), titleBuffer, TITLE_SIZE);
    titleBuffer[TITLE_SIZE - 1] = 0; // ensure string termination
    title.invalidate();

    switch (mNotification->getCategory()) {
        case PhoneNotification::Category::ACTIVE_CALL:
            body.setContent(T_TEXT_ACTIVE_CALL);
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            body.setContent(T_TEXT_INCOMING_CALL);
            break;
        case PhoneNotification::Category::MISSED_CALL:
            body.setContent(T_TEXT_MISSED_CALL);
            break;
        case PhoneNotification::Category::EMAIL:
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            if (!mNotification->getSubTitle().empty() && !mNotification->getMsg().empty()) {
                body.setContent(reinterpret_cast<const uint8_t *>(mNotification->getSubTitle().c_str()), reinterpret_cast<const uint8_t *>(mNotification->getMsg().c_str()));
            } else if (!mNotification->getSubTitle().empty() ){ 
                body.setContent(reinterpret_cast<const uint8_t *>(mNotification->getSubTitle().c_str()), nullptr);
            } else if (!mNotification->getMsg().empty()) { 
                body.setContent(nullptr, reinterpret_cast<const uint8_t *>(mNotification->getMsg().c_str()));
            }
            break;
    }

    sideBarSmall.setCount(body.getNumberOfItems());

    updateScroll();
}

void PhoneNotificationDetailsView::handleTickEvent()
{
    // Exit if there in nothing to show
    if (mNotification == nullptr) {
        presenter->exit();
        return;
    }
}

void PhoneNotificationDetailsView::handleKeyEvent(uint8_t key)
{
    // Exit if there in nothing to show
    if (mNotification == nullptr) {
        presenter->exit();
        return;
    }

    if (key == Gui::Config::Button::L1) {
        body.selectPrev();
        updateScroll();
    }

    if (key == Gui::Config::Button::L2) {
        body.selectNext();
        updateScroll();
    }

    if (key == Gui::Config::Button::R1) {
 
    }

    if (key == Gui::Config::Button::R2) {
        if (mNotification->isRead()) {
            presenter->negAction(mNotification);
        }
        presenter->exit();
    }
}

void PhoneNotificationDetailsView::setCategoryStyle(PhoneNotification::Category category)
{
    imageCallMissed.setVisible(false);
    imageEmail.setVisible(false);
    imageMessage.setVisible(false);


    switch (category) {
        case PhoneNotification::Category::ACTIVE_CALL:
        case PhoneNotification::Category::INCOMING_CALL:
            break;
        case PhoneNotification::Category::MISSED_CALL:
            imageCallMissed.setVisible(true);
            break;
        case PhoneNotification::Category::EMAIL:
            imageEmail.setVisible(true);
            break;
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            imageMessage.setVisible(true);
            break;
    }

    imageCallMissed.invalidate();
    imageEmail.invalidate();
    imageMessage.invalidate();
}

void PhoneNotificationDetailsView::updateScroll()
{
    if (body.getNumberOfItems() > 1) {
        ellipsisUp.setVisible(body.getSelectedItem() > 0);
        ellipsisUp.invalidate();
        ellipsisDown.setVisible(body.getSelectedItem() < (body.getNumberOfItems() - 1));
        ellipsisDown.invalidate();
    }

    //sideBarSmall.animateToId(body.getSelectedItem(), Gui::Config::kMenuAnimationSteps);
    sideBarSmall.setActiveId(body.getSelectedItem());

    if (!mNotification->isRead()) {
        if (body.getSelectedItem() == (body.getNumberOfItems() - 1)) {
            mNotification->markAsRead();
        }
    }
    buttons.setR2(mNotification->isRead() ? Buttons::RED : Buttons::WHITE);
}
