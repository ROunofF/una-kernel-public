#include <gui/phonenotificationconfirm_screen/PhoneNotificationConfirmView.hpp>

PhoneNotificationConfirmView::PhoneNotificationConfirmView()
{

}

void PhoneNotificationConfirmView::setupScreen()
{
    PhoneNotificationConfirmViewBase::setupScreen();

    title.set(T_TEXT_NOTIF_DOT_UC);
}

void PhoneNotificationConfirmView::tearDownScreen()
{
    PhoneNotificationConfirmViewBase::tearDownScreen();
}

void PhoneNotificationConfirmView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {
        
    }

    if (key == Gui::Config::Button::R1) {
        presenter->clearAll();
        application().gotoPhoneNotificationListScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoPhoneNotificationListScreenNoTransition();
    }
}