#include <gui/settingswatchface_screen/SettingsWatchFaceView.hpp>

SettingsWatchFaceView::SettingsWatchFaceView()
{

}

void SettingsWatchFaceView::setupScreen()
{
    SettingsWatchFaceViewBase::setupScreen();
}

void SettingsWatchFaceView::tearDownScreen()
{
    // The descriptor pool is small and shared, so the slot goes back as soon as
    // this screen stops showing a face.
    appFrame.detach();

    SettingsWatchFaceViewBase::tearDownScreen();
}

void SettingsWatchFaceView::setWatchFace(uint32_t guiPid)
{
    mPid = guiPid;

    if (mPid == 0) {
        // The built-in face is this screen's own, so the app frame comes down
        // and what is behind it is already the right thing to show.
        appFrame.hideFrame();
        watchFace1.setVisible(true);
        watchFace1.invalidate();
        return;
    }

    // A face app is being shown, so this screen's own face has no business
    // standing in for it: the two are different clocks, and one wearing the
    // other's place is what the screen is here to let the user judge. Down it
    // goes, and the background is what shows until the face has drawn.
    //
    // Only ever the built-in one is taken down. A face already on the surface
    // stays up until the next has something to replace it with, so stepping
    // between two faces does not flash in between.
    //
    // Nor if there is nowhere for the app's pixels to go: no frame can ever
    // arrive then, and taking this down would leave the screen bare for good.
    if (appFrame.isAttached() && watchFace1.isVisible()) {
        watchFace1.setVisible(false);
        watchFace1.invalidate();
    }

    refreshFromSurface();
}

void SettingsWatchFaceView::setCursor(uint8_t count, uint8_t index)
{
    chrome.setCursor(count, index);
    chrome.show();
}

void SettingsWatchFaceView::stepCursor(int8_t delta)
{
    chrome.stepCursor(delta);
    chrome.show();
}

void SettingsWatchFaceView::setAppSurface(const AppSurfaceInfo &info)
{
    mSurfaceInfo = info;

    // appFrame sits below watchFace1 by the design, so nothing here decides
    // where a face's pixels are in the z-order.
    if (!appFrame.attach(info.pixels, info.width, info.height)) {
        // Nothing will ever be drawn there, which is also what the simulator
        // gets. The built-in face simply stays up.
        return;
    }

    refreshFromSurface();
}

void SettingsWatchFaceView::refreshFromSurface()
{
    uint32_t owner = 0;
    uint32_t frame = 0;

    if (mPid == 0 || !mSurfaceInfo.sample(owner, frame)) {
        return;
    }

    // An unowned surface is checked on its own rather than left to the
    // comparison: a zero pid would otherwise match a surface nobody holds and
    // take the last app's pixels for the previewed face's own.
    if (owner == 0 || owner != mPid || frame == 0) {
        // Nothing of this face's to show yet. What is up stays: stepping
        // between faces should not flash the screen in between.
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

void SettingsWatchFaceView::setDailyHealth(uint32_t steps, uint32_t stepsGoal,
    uint32_t floors, uint32_t floorsGoal,
    uint32_t activity, uint32_t activityGoal)
{
    watchFace1.setTopBarProgress(steps, stepsGoal);
    watchFace1.setBotBarProgress(activity, activityGoal);
}

void SettingsWatchFaceView::setMuteAlerts(bool muted)
{
    watchFace1.setMuteAlerts(muted);
}

void SettingsWatchFaceView::setTime(uint8_t h, uint8_t m, uint8_t s)
{
    watchFace1.setTime(h, m, s);
}

void SettingsWatchFaceView::setClockSettings(const WatchSettingsLocal::Clock &clock)
{
    watchFace1.setClockFormat(clock.format);
    watchFace1.setClockSeconds(clock.seconds);
    watchFace1.setClockDateOrder(clock.dateOrder);
}

void SettingsWatchFaceView::setDate(uint8_t m, uint8_t d, uint8_t wd)
{
    watchFace1.setDate(m, d, wd);
}

void SettingsWatchFaceView::setBatteryLevel(uint8_t level)
{
    watchFace1.setBatteryLevel(level);
}

void SettingsWatchFaceView::handleTickEvent()
{
    refreshFromSurface();
}

void SettingsWatchFaceView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        presenter->selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        presenter->selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        presenter->apply();
        application().gotoSettingsFaceSavedScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsScreenNoTransition();
    }
}
