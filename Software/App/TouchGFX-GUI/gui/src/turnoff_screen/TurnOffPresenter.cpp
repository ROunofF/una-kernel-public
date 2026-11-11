#include <gui/turnoff_screen/TurnOffView.hpp>
#include <gui/turnoff_screen/TurnOffPresenter.hpp>

TurnOffPresenter::TurnOffPresenter(TurnOffView& v)
    : view(v)
{

}

void TurnOffPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();
}

void TurnOffPresenter::deactivate()
{

}
