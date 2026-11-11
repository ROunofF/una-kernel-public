#include <gui/phonenotificationdetails_screen/PhoneNotificationDetailsView.hpp>
#include <gui/phonenotificationdetails_screen/PhoneNotificationDetailsPresenter.hpp>

PhoneNotificationDetailsPresenter::PhoneNotificationDetailsPresenter(PhoneNotificationDetailsView& v)
    : view(v)
{

}

void PhoneNotificationDetailsPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    uint32_t uid = model->getPosNotificationListUid();
    if (uid == 0) {
        uid = model->phoneNotificationGetUidForDetails();
    }

    if (uid != 0) {
        std::shared_ptr<PhoneNotification> notification = model->phoneNotificationGetByUID(uid);
        view.setNotification(notification);
    }
}

void PhoneNotificationDetailsPresenter::deactivate()
{
    model->phoneNotificationSetUidForDetails(0);
}

void PhoneNotificationDetailsPresenter::negAction(const std::shared_ptr<PhoneNotification> &notification)
{
    model->phoneNotificationNegativeAction(notification);
}

void PhoneNotificationDetailsPresenter::exit()
{
    if (model->getPosNotificationListUid()) {
        // Enter here from notification list. so go there
        model->application().gotoPhoneNotificationListScreenNoTransition();
    } else { 
        model->switchToNextPriorityScreen();
    }
}