#include <gui/shutdown_screen/ShutDownView.hpp>

ShutDownView::ShutDownView()
{

}

void ShutDownView::setupScreen()
{
    ShutDownViewBase::setupScreen();
}

void ShutDownView::tearDownScreen()
{
    ShutDownViewBase::tearDownScreen();
}

void ShutDownView::handleTickEvent()
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