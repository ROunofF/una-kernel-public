#include <gui/appstartfailed_screen/AppStartFailedView.hpp>

#include <texts/TextKeysAndLanguages.hpp>

AppStartFailedView::AppStartFailedView()
{

}

void AppStartFailedView::setupScreen()
{
    AppStartFailedViewBase::setupScreen();
}

void AppStartFailedView::tearDownScreen()
{
    AppStartFailedViewBase::tearDownScreen();
}

void AppStartFailedView::setReason(App::LaunchFailure reason)
{
    // Only the memory case gets its own wording. The rest are things the user
    // can do nothing about and cannot tell apart - a bad image, an app that
    // did not answer, an icon for something no longer installed - so naming
    // them would be detail without a decision attached.
    const TEXTS textId = (reason == App::LaunchFailure::OUT_OF_MEMORY)
                       ? T_TEXT_APP_START_FAILED_MEMORY
                       : T_TEXT_APP_START_FAILED_GENERIC;

    message.setTypedText(touchgfx::TypedText(textId));
    message.invalidate();
}

void AppStartFailedView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        // Hand the user back to wherever the launch was started - the top
        // menu, the tracking menu, or the watch face. Deliberately NOT
        // UserApp: that is where a RUNNING app draws, and nothing is running,
        // so it would show the placeholder for an app that never started.
        // The model owns the one mapping from origin to screen.
        presenter->returnToLauncher();
    }
}
