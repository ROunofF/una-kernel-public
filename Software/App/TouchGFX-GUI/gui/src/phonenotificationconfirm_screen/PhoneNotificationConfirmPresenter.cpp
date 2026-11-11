#include <gui/phonenotificationconfirm_screen/PhoneNotificationConfirmView.hpp>
#include <gui/phonenotificationconfirm_screen/PhoneNotificationConfirmPresenter.hpp>

PhoneNotificationConfirmPresenter::PhoneNotificationConfirmPresenter(PhoneNotificationConfirmView& v)
    : view(v)
{

}

void PhoneNotificationConfirmPresenter::activate()
{

}

void PhoneNotificationConfirmPresenter::deactivate()
{

}

void PhoneNotificationConfirmPresenter::clearAll()
{
    model->phoneNotificationClearAll();
}