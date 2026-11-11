#include <gui/lowbattshutdown_screen/LowBattShutDownView.hpp>
#include <gui/lowbattshutdown_screen/LowBattShutDownPresenter.hpp>

LowBattShutDownPresenter::LowBattShutDownPresenter(LowBattShutDownView& v)
    : view(v)
{

}

void LowBattShutDownPresenter::activate()
{

}

void LowBattShutDownPresenter::deactivate()
{

}

void LowBattShutDownPresenter::shutdown()
{
    model->shutdown();
}