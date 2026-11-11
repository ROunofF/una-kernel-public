#include <gui/userapp_screen/UserAppView.hpp>
#include <gui/common/GuiConfig.hpp>

// How long an app gets to produce its first frame before the screen says which
// app it is waiting for. A healthy app is far quicker, so the label appearing
// at all means something is wrong with it.
static constexpr uint16_t kFirstFrameTicks = GUI_CONFIG_MS_2_TICKS(1000);

UserAppView::UserAppView()
    : mFirstFrameCb(this, &UserAppView::onFirstFrameOverdue)
{

}

void UserAppView::setupScreen()
{
    UserAppViewBase::setupScreen();
}

void UserAppView::tearDownScreen()
{
    mFirstFrameTimer.stop();

    // The descriptor pool is small and shared, so the slot goes back as soon as
    // this screen stops showing an app.
    appFrame.detach();

    UserAppViewBase::tearDownScreen();
}

void UserAppView::setApp(uint32_t pid)
{
    // A start request can name the app already on screen -- the backend raises
    // one whenever an app is brought to the front, whether or not that changes
    // which app that is. Only an actual change is a reason to take down a frame
    // being drawn normally and start waiting again.
    const bool changed = (pid != mPid);

    mPid = pid;

    Unicode::snprintf(nameBuffer, NAME_SIZE, "App PID\n0x%08X", pid);
    name.invalidate();

    // Entering the screen and swapping one app for another in place both land
    // here, and neither has a frame of the new app's yet, so this is the one
    // place the wait is armed. Doing it in setAppSurface() instead would miss
    // the swap, which does not go through the presenter's activate().
    //
    // On the way in this runs before the surface is attached, since activate()
    // calls setApp() first, so the appFrame calls below reach a widget with no
    // bitmap. That is allowed and does nothing; setAppSurface() attaches and
    // refreshes immediately afterwards.
    if (changed) {
        awaitFirstFrame();
    }

    refreshFromSurface();
}

void UserAppView::setAppSurface(const AppSurfaceInfo &info)
{
    mSurfaceInfo = info;

    // appFrame is placed by the design, between the background and everything
    // else, so nothing here decides where the app's pixels sit in the z-order.
    if (!appFrame.attach(info.pixels, info.width, info.height)) {
        // Nothing will ever be drawn, so name the app rather than wait for a
        // frame that cannot arrive. This is also what the simulator gets, since
        // no app runs there.
        mFirstFrameTimer.stop();
        name.setVisible(true);
        name.invalidate();
        return;
    }

    refreshFromSurface();

    invalidate();
}

void UserAppView::awaitFirstFrame()
{
    // Whatever is on the surface belongs to whoever had it before, so it comes
    // down rather than standing in for the app now being started.
    appFrame.hideFrame();

    name.setVisible(false);

    // Normally the wait lasts a fraction of a second and showing anything for
    // it would only flicker, so the label is on a delay.
    mFirstFrameTimer.setDuration(kFirstFrameTicks);
    mFirstFrameTimer.setCallback(mFirstFrameCb);
    mFirstFrameTimer.start();
}

void UserAppView::refreshFromSurface()
{
    uint32_t owner = 0;
    uint32_t frame = 0;

    if (!mSurfaceInfo.sample(owner, frame)) {
        return;
    }

    // An unowned surface is checked on its own rather than left to the
    // comparison: this screen can be up with no app of its own, and a zero pid
    // would then match a surface nobody holds and take the last app's pixels
    // for its own. AppSurface::write() refuses a zero pid for the same reason.
    if (owner == 0 || owner != mPid || frame == 0) {
        // No frame of this app's on the surface: it has not been handed over
        // yet, or the app has given it up on the way out, or it owns the
        // surface and has drawn nothing. What is on screen stays -- blanking
        // on the way out would flash black for the tick or two before the
        // screen changes.
        return;
    }

    if (!appFrame.showFrame(owner, frame)) {
        return;
    }

    mFirstFrameTimer.stop();

    if (name.isVisible()) {
        name.setVisible(false);
        name.invalidate();
    }
}

void UserAppView::handleKeyEvent(uint8_t key)
{
//    if (key == Gui::Config::Button::R2) {
//        application().gotoHomeScreenNoTransition();
//    }
}

void UserAppView::handleTickEvent()
{
    if (mPid == 0) {
        presenter->onAppStop();
        return;
    }

    refreshFromSurface();
}

void UserAppView::onFirstFrameOverdue()
{
    // An app this slow is not merely loading. Naming it beats a blank screen,
    // which reads as the watch being off. Nothing here acts on it: an app that
    // never draws at all is terminated by the manager's liveness check, and one
    // that draws once and then stops is caught by neither.
    name.setVisible(true);
    name.invalidate();
}
