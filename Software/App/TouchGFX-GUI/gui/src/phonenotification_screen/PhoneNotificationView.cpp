#include <gui/phonenotification_screen/PhoneNotificationView.hpp>

PhoneNotificationView::PhoneNotificationView()
{

}

void PhoneNotificationView::setupScreen()
{
    PhoneNotificationViewBase::setupScreen();
}

void PhoneNotificationView::tearDownScreen()
{
    PhoneNotificationViewBase::tearDownScreen();
}

void PhoneNotificationView::setNotification(std::shared_ptr<PhoneNotification> notification)
{
    if (notification == nullptr) {
        return;
    }

    if (mNotification != notification) {
        if (mNotification) {
            if (!isHigherPriority(notification->getCategory(), mNotification->getCategory())) {
                return;
            }
        }

        presenter->play();
        mNotification = notification;
    }

    mNotification->markAsShown();
    mCounter = skTimeout;

    setCategoryStyle(mNotification->getCategory());

    Unicode::fromUTF8(reinterpret_cast<const uint8_t *>(mNotification->getTitle().c_str()), titleBuffer, TITLE_SIZE);
    titleBuffer[TITLE_SIZE - 1] = 0; // ensure string termination
    title.invalidate();

    Unicode::snprintf(timeBuffer, TIME_SIZE, "%02u:%02u", mNotification->getTimestamp().tm_hour, mNotification->getTimestamp().tm_min);
    time.invalidate();

    switch (mNotification->getCategory()) {
        case PhoneNotification::Category::ACTIVE_CALL:
            fillActiveCallTime();
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            Unicode::snprintf(bodyBuffer, BODY_SIZE, "%s", touchgfx::TypedText(T_TEXT_INCOMING_CALL).getText());
            break;
        case PhoneNotification::Category::MISSED_CALL:
            Unicode::snprintf(bodyBuffer, BODY_SIZE, "%s", touchgfx::TypedText(T_TEXT_MISSED_CALL).getText());
            break;
        case PhoneNotification::Category::EMAIL:
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            if (!notification->getSubTitle().empty()) {
                Unicode::fromUTF8(reinterpret_cast<const uint8_t *>(mNotification->getSubTitle().c_str()), bodyBuffer, BODY_SIZE);
            } else {
                Unicode::fromUTF8(reinterpret_cast<const uint8_t *>(mNotification->getMsg().c_str()), bodyBuffer, BODY_SIZE);
            }
            break;
    }

    bodyBuffer[BODY_SIZE - 1] = 0; // ensure string termination
    body.invalidate();
}

void PhoneNotificationView::removed(uint32_t uid, bool all)
{
    if (all || (mNotification != nullptr && mNotification->getUid() == uid)) {

        if (mNotification != nullptr) { 
            mNotification->markAsRead();
        }
        presenter->close();
    }
}

uint32_t PhoneNotificationView::getUid()
{
    if (mNotification != nullptr) {
        return mNotification->getUid();
    }
    return 0;
}

bool PhoneNotificationView::isCall()
{
    if (mNotification != nullptr) {
        return mNotification->getCategory() == PhoneNotification::Category::ACTIVE_CALL ||
            mNotification->getCategory() == PhoneNotification::Category::INCOMING_CALL;
    }
    return false;
}

void PhoneNotificationView::handleTickEvent()
{
    if (mNotification == nullptr) {
        presenter->close();
        return;
    }

    if (mNotification->getCategory() == PhoneNotification::Category::ACTIVE_CALL) {
        fillActiveCallTime();
    } else if (mNotification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
        if (mIncominCallCounter > 0) {
            mIncominCallCounter--;
        }
        if (mIncominCallCounter == 0) {
            // Restart notification sound/vibro
            mIncominCallCounter = skIncominCallPeriod;
            presenter->play();
        }
    } else {
        if (mCounter > 0) {
            mCounter--;
        }
        if (mCounter == 0) {
            presenter->close();
        }
    }
}

void PhoneNotificationView::handleKeyEvent(uint8_t key)
{
    if (mNotification == nullptr) {
        presenter->close();
        return;
    }

    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {
        if (mNotification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
            mNotification->markAsRead();
            presenter->negAction(mNotification);
        }
    }

    if (key == Gui::Config::Button::R1) {
        if (mNotification->getCategory() == PhoneNotification::Category::ACTIVE_CALL) { 
            mNotification->markAsRead();
            presenter->negAction(mNotification);
        } else if (mNotification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
            mNotification->markAsRead();
            presenter->posAction(mNotification);
            presenter->stopPlay();
        } else {
            // Read details
            presenter->setDetailsUid(mNotification->getUid());
            application().gotoPhoneNotificationDetailsScreenNoTransition();
        }
    }

    if (key == Gui::Config::Button::R2) {
        if (mNotification->getCategory() == PhoneNotification::Category::ACTIVE_CALL) {
            // Do nothing
        } else if (mNotification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
            // Mute
            mNotification->markAsRead();
            presenter->stopPlay();
        } else {
            // Hide message
            presenter->close();
        }
    }
}

void PhoneNotificationView::setCategoryStyle(PhoneNotification::Category category)
{ 
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::NONE);

    labelMute.setVisible(false);
    labelDecline.setVisible(false);
    labelEndCall.setVisible(false);
    labelAnswer.setVisible(false);

    imageCallActive.setVisible(false);
    imageCallIncoming.setVisible(false);
    imageCallMissed.setVisible(false);
    imageEmail.setVisible(false);
    imageMessage.setVisible(false);

    time.setVisible(false);

    title.setX(20);
    title.setWidth(200);
    title.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_SEMIBOLD_18));
    body.setX(20);
    body.setWidth(200);
    body.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_ITALIC_18));


    switch (category) {
        case PhoneNotification::Category::ACTIVE_CALL:
            buttons.setR1(Buttons::RED);
            labelEndCall.setVisible(true);

            time.setVisible(true);

            imageCallActive.setVisible(true);

            title.setX(75);
            title.setWidth(145);
            title.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_SEMIBOLD_18_L));
            body.setX(75);
            body.setWidth(145);
            body.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_ITALIC_18_L));
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            buttons.setR1(Buttons::AMBER);
            labelAnswer.setVisible(true);

            buttons.setR2(Buttons::WHITE);
            labelMute.setVisible(true);

            buttons.setL2(Buttons::RED);
            labelDecline.setVisible(true);

            time.setVisible(true);

            imageCallIncoming.setVisible(true);

            title.setX(75);
            title.setWidth(145);
            title.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_SEMIBOLD_18_L));
            body.setX(75);
            body.setWidth(145);
            body.setTypedText(touchgfx::TypedText(T_TMP_MONTSERRAT_ITALIC_18_L));
            break;
        case PhoneNotification::Category::MISSED_CALL:
            buttons.setR1(Buttons::AMBER);
            buttons.setR2(Buttons::WHITE);
            imageCallMissed.setVisible(true);
            break;
        case PhoneNotification::Category::EMAIL:
            buttons.setR1(Buttons::AMBER);
            buttons.setR2(Buttons::WHITE);
            imageEmail.setVisible(true);
            break;
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
        default:
            buttons.setR1(Buttons::AMBER);
            buttons.setR2(Buttons::WHITE);
            imageMessage.setVisible(true);
            break;
    }


    labelMute.invalidate();
    labelDecline.invalidate();
    labelEndCall.invalidate();
    labelAnswer.invalidate();

    imageCallActive.invalidate();
    imageCallIncoming.invalidate();
    imageCallMissed.invalidate();
    imageEmail.invalidate();
    imageMessage.invalidate();

    time.invalidate();

    title.invalidate();
    body.invalidate();
}

void PhoneNotificationView::fillActiveCallTime()
{
    uint32_t timePass = presenter->getTimePassMs(mNotification->getRxTimeMs()) / 1000;
    uint16_t h = 0;
    uint8_t m = 0;
    uint8_t s = 0;
    Gui::Utils::sec2hms(timePass, h, m, s);

    if (h > 0) {
        Unicode::snprintf(bodyBuffer, BODY_SIZE, "%u:%02u:%02u", h, m, s);
    } else {
        Unicode::snprintf(bodyBuffer, BODY_SIZE, "%u:%02u", m, s);
    }
    bodyBuffer[BODY_SIZE - 1] = 0; // ensure string termination
    body.invalidate();
}

bool PhoneNotificationView::isHigherPriority(PhoneNotification::Category n, PhoneNotification::Category o)
{
    switch (n) { 
        case PhoneNotification::Category::ACTIVE_CALL:
            return true;
            break;
        case PhoneNotification::Category::INCOMING_CALL:
            return o != PhoneNotification::Category::INCOMING_CALL; // Discard new incoming call
            break;
        case PhoneNotification::Category::MISSED_CALL:
        case PhoneNotification::Category::EMAIL:
        case PhoneNotification::Category::MESSAGE:
        case PhoneNotification::Category::UNSPECIFIED:
            return (o != PhoneNotification::Category::ACTIVE_CALL && o != PhoneNotification::Category::INCOMING_CALL);
            break;
    }
    return false;
}
