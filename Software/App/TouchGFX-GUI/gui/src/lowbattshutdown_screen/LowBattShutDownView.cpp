#include <gui/lowbattshutdown_screen/LowBattShutDownView.hpp>

LowBattShutDownView::LowBattShutDownView()
{

}

void LowBattShutDownView::setupScreen()
{
    LowBattShutDownViewBase::setupScreen();
}

void LowBattShutDownView::tearDownScreen()
{
    LowBattShutDownViewBase::tearDownScreen();
}

void LowBattShutDownView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        if (mCommandSent == false) {
            mCommandSent = true;
            presenter->shutdown();
        }
    }
}
