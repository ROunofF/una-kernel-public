#include <gui/paired_screen/PairedView.hpp>

PairedView::PairedView()
{

}

void PairedView::setupScreen()
{
    PairedViewBase::setupScreen();
}

void PairedView::tearDownScreen()
{
    PairedViewBase::tearDownScreen();
}

void PairedView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        presenter->exit();
    }
}