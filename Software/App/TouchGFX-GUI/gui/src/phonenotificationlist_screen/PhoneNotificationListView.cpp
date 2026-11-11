#include <gui/phonenotificationlist_screen/PhoneNotificationListView.hpp>

PhoneNotificationListView::PhoneNotificationListView()
{

}

void PhoneNotificationListView::setupScreen()
{
    PhoneNotificationListViewBase::setupScreen();

    title.set(T_TEXT_NOTIF_DOT_UC);
}

void PhoneNotificationListView::tearDownScreen()
{
    PhoneNotificationListViewBase::tearDownScreen();
}

void PhoneNotificationListView::setTime(const std::tm &time)
{
    menu.setTime(time);
}

void PhoneNotificationListView::setList(const std::vector<std::shared_ptr<PhoneNotification>> &list)
{
    menu.setList(list);
}

void PhoneNotificationListView::setPositionUid(uint32_t uid)
{
    menu.selectItemUid(uid);
}

uint32_t PhoneNotificationListView::getPositionUid()
{
    return menu.getUidOfSelectedItem();
}

void PhoneNotificationListView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        uint16_t selected = menu.getSelectedItem();
        if (selected > 0) {
            application().gotoPhoneNotificationDetailsScreenNoTransition();
        } else { 
            application().gotoPhoneNotificationConfirmScreenNoTransition();
        }
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoGlancesScreenNoTransition();
    }
}
