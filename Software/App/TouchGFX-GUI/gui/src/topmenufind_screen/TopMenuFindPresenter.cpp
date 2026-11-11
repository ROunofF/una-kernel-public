#include <gui/topmenufind_screen/TopMenuFindView.hpp>
#include <gui/topmenufind_screen/TopMenuFindPresenter.hpp>

TopMenuFindPresenter::TopMenuFindPresenter(TopMenuFindView& v)
    : view(v)
{

}

void TopMenuFindPresenter::activate()
{
    model->ringing(true);
}

void TopMenuFindPresenter::deactivate()
{
    model->ringing(false);
}

void TopMenuFindPresenter::onFindPhoneConfirmed()
{
    model->switchToNextPriorityScreen();
}
