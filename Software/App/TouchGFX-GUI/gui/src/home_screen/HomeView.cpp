#include <gui/home_screen/HomeView.hpp>

HomeView::HomeView()
{

}

void HomeView::setupScreen()
{
    HomeViewBase::setupScreen();
}

void HomeView::tearDownScreen()
{
    // The descriptor pool is small and shared, so the slot goes back as soon as
    // this screen stops showing a face.
    appFrame.detach();

    HomeViewBase::tearDownScreen();
}

void HomeView::setWatchFace(uint32_t guiPid)
{
    mPid = guiPid;

    if (mPid == 0) {
        // The built-in face draws this screen: either it was the choice all
        // along, or whatever was drawing it has gone and left the surface with
        // an owner of nobody, which nothing else here would notice.
        appFrame.hideFrame();
        watchFace1.setVisible(true);
        watchFace1.invalidate();
        return;
    }

    // A face app draws this screen, so the built-in one has no business being
    // seen even for a frame -- the two are different clocks, and either would
    // read as the watch changing its mind. Down it goes, and what is left is
    // the background until the face has drawn.
    // Not if there is nowhere for the app's pixels to go: no frame can ever
    // arrive then, and taking this down would leave the screen bare for good.
    if (appFrame.isAttached() && watchFace1.isVisible()) {
        watchFace1.setVisible(false);
        watchFace1.invalidate();
    }

    // Nothing of the face's is up yet either: a face that has stood down gives
    // the surface back, so what is on it belongs to whoever came before and is
    // not this moment.
    appFrame.hideFrame();

    refreshFromSurface();
}

void HomeView::setAppSurface(const AppSurfaceInfo &info)
{
    mSurfaceInfo = info;

    // appFrame sits below watchFace1 by the design, so nothing here decides
    // where the face's pixels are in the z-order.
    if (!appFrame.attach(info.pixels, info.width, info.height)) {
        // Nothing will ever be drawn there, which is also what the simulator
        // gets. The built-in face simply stays up.
        return;
    }

    refreshFromSurface();
}

void HomeView::refreshFromSurface()
{
    uint32_t owner = 0;
    uint32_t frame = 0;

    if (mPid == 0 || !mSurfaceInfo.sample(owner, frame)) {
        return;
    }

    // An unowned surface is checked on its own rather than left to the
    // comparison: a zero pid would otherwise match a surface nobody holds and
    // take the last app's pixels for the face's own.
    if (owner == 0 || owner != mPid || frame == 0) {
        return;
    }

    if (!appFrame.showFrame(owner, frame)) {
        return;
    }

    if (watchFace1.isVisible()) {
        watchFace1.setVisible(false);
        watchFace1.invalidate();
    }
}

void HomeView::setDailyHealth(uint32_t steps, uint32_t stepsGoal,
    uint32_t floors, uint32_t floorsGoal,
    uint32_t activity, uint32_t activityGoal)
{
    watchFace1.setTopBarProgress(steps, stepsGoal);
    watchFace1.setBotBarProgress(activity, activityGoal);
}

void HomeView::setHomeWidget(const HomeWidgetData &widget)
{
    watchFace1.setHomeWidget(widget);
}

void HomeView::setMuteAlerts(bool muted)
{
    watchFace1.setMuteAlerts(muted);
}

void HomeView::setTime(uint8_t h, uint8_t m, uint8_t s)
{
    watchFace1.setTime(h, m, s);
}

void HomeView::setClockSettings(const WatchSettingsLocal::Clock &clock)
{
    watchFace1.setClockFormat(clock.format);
    watchFace1.setClockSeconds(clock.seconds);
    watchFace1.setClockDateOrder(clock.dateOrder);
}

void HomeView::setDate(uint8_t m, uint8_t d, uint8_t wd)
{
    watchFace1.setDate(m, d, wd);
}

void HomeView::setBatteryLevel(uint8_t level)
{
    watchFace1.setBatteryLevel(level);
}

void HomeView::handleTickEvent()
{
    refreshFromSurface();
}

void HomeView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        application().gotoTopMenuScreenNoTransition();
    }

    if (key == Gui::Config::Button::L2) {
        application().gotoGlancesScreenNoTransition();
    }

    if (key == Gui::Config::Button::R1) {
        application().gotoTrackingMenuScreenNoTransition();
    }
}