#include <gui/phonenotification_screen/PhoneNotificationView.hpp>
#include <gui/phonenotification_screen/PhoneNotificationPresenter.hpp>

PhoneNotificationPresenter::PhoneNotificationPresenter(PhoneNotificationView& v)
    : view(v)
{

}

void PhoneNotificationPresenter::activate()
{
    view.setNotification(model->phoneNotificationGetNextActive());
    model->phoneNotificationSetUidForDetails(0);
    model->setPosNotificationListUid(0);
}

void PhoneNotificationPresenter::deactivate()
{
    stopPlay();
}

void PhoneNotificationPresenter::onPhoneNotificationNew(std::shared_ptr<PhoneNotification> notification)
{
    view.setNotification(notification);
}

void PhoneNotificationPresenter::onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list)
{
    uint32_t currentUid = view.getUid();

    std::shared_ptr<PhoneNotification> notification = model->phoneNotificationGetByUID(currentUid);
    if (notification) {
        view.setNotification(notification);
    }
}

void  PhoneNotificationPresenter::onPhoneNotificationRemoved(uint32_t uid, bool all)
{
    view.removed(uid, all);
}

void PhoneNotificationPresenter::play()
{
    model->phoneNotificationPlay();
}

void PhoneNotificationPresenter::stopPlay()
{
    model->phoneNotificationStop();
}

void PhoneNotificationPresenter::close()
{
    model->switchToNextPriorityScreen();
}

void PhoneNotificationPresenter::setDetailsUid(uint32_t uid)
{
    model->phoneNotificationSetUidForDetails(uid);
}

void PhoneNotificationPresenter::posAction(const std::shared_ptr<PhoneNotification> &notification)
{
    model->phoneNotificationPositiveAction(notification);
}

void PhoneNotificationPresenter::negAction(const std::shared_ptr<PhoneNotification> &notification)
{
    model->phoneNotificationNegativeAction(notification);
}

uint32_t PhoneNotificationPresenter::getTimePassMs(uint32_t notificationRxTimeMs)
{
    uint32_t timeNow = model->getTimeMs();
    if (timeNow > notificationRxTimeMs) {
        return (timeNow - notificationRxTimeMs);
    }

    return 0;
}

