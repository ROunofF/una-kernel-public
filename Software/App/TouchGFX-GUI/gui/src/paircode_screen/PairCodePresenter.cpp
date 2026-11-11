#include <gui/paircode_screen/PairCodeView.hpp>
#include <gui/paircode_screen/PairCodePresenter.hpp>

PairCodePresenter::PairCodePresenter(PairCodeView& v)
    : view(v)
{
}

void PairCodePresenter::activate()
{
    view.setCode(model->bleGetPinCode());

    // Reset idle timer
    model->resetIdleTimer();
}

void PairCodePresenter::deactivate()
{
}

void PairCodePresenter::onBleConnect(bool connected)
{
    if (connected == false) {
        onBlePair(false);
    }
}

void PairCodePresenter::onBlePair(bool paired)
{
    if (paired) {
        model->application().gotoPairedScreenNoTransition();
    } else {
        if (model->bleIsPairingManual()) {
            // Previous screen
            model->bleAbortPairing();
            model->application().gotoQRCodeScreenNoTransition();
        } else {
            model->switchToNextPriorityScreen();
        }
    }
}

void PairCodePresenter::abortPairing()
{
    if (model->bleIsPairingManual()) {
        model->bleAbortPairing();
        model->application().gotoQRCodeScreenNoTransition();
    } else { 
        model->bleAbortPairing();
        model->switchToNextPriorityScreen();
    }
}
