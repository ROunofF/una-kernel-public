#include <gui/error_screen/ErrorView.hpp>
#include <gui/error_screen/ErrorPresenter.hpp>

ErrorPresenter::ErrorPresenter(ErrorView& v)
    : view(v)
{

}

void ErrorPresenter::activate()
{
    view.setError(model->getErrorToShow());
    model->systemErrorPlay(true);
}

void ErrorPresenter::deactivate()
{
    // Only stop the buzzer. Leaving the screen must NOT clear the error: the
    // condition is owned by its producer, and dismissal goes through
    // acknowledge() instead.
    model->systemErrorPlay(false);
}

void ErrorPresenter::onError()
{
    if (model->getErrorToShow() == SystemError::NONE) {
        model->switchToNextPriorityScreen();          // nothing left - leave
    } else {
        view.setError(model->getErrorToShow());       // new code - update in place
    }
}

void ErrorPresenter::acknowledge()
{
    model->acknowledgeError();
}

void ErrorPresenter::play()
{
    model->systemErrorPlay(true);
}

void ErrorPresenter::silence()
{
    model->systemErrorPlay(false);
}

void ErrorPresenter::exitScreen()
{
    model->switchToNextPriorityScreen();
}
