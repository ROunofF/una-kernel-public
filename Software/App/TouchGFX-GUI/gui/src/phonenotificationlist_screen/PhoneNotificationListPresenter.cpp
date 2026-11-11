#include <gui/phonenotificationlist_screen/PhoneNotificationListView.hpp>
#include <gui/phonenotificationlist_screen/PhoneNotificationListPresenter.hpp>

PhoneNotificationListPresenter::PhoneNotificationListPresenter(PhoneNotificationListView& v)
    : view(v)
{

}

void PhoneNotificationListPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    view.setTime(model->getDateTime());
    view.setList(model->phoneNotificationGetAll());
    view.setPositionUid(model->getPosNotificationListUid());
}

void PhoneNotificationListPresenter::deactivate()
{
    model->phoneNotificationSetUidForDetails(view.getPositionUid());
    model->setPosNotificationListUid(view.getPositionUid());
}

void PhoneNotificationListPresenter::onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list)
{
    view.setList(list);
}
