#include <gui/settingscompass_screen/SettingsCompassView.hpp>

#include <gui/common/GuiConfig.hpp>

#include <cmath>

namespace {
constexpr float    kFullSweep    = 360.0f;

// Run length; a run too short to cover the sphere is worse than none.
constexpr uint32_t kRunSeconds   = 30;

// How long "Didn't work" stays up before the screen goes back to resting.
constexpr uint16_t kFailedTicks  = GUI_CONFIG_MS_2_TICKS(2500);

// A face turned too far from the sky shows no bearing: a rose drawn on it
// cannot be read against the ground, although the bearing is there. A face
// showing one keeps it up to kHideFaceTiltDeg; a face that is not gets one back
// only at kShowFaceTiltDeg, so a face held on the edge does not make it blink.
constexpr float    kShowFaceTiltDeg = 70.0f;
constexpr float    kHideFaceTiltDeg = 75.0f;

// How far a reading may wander before the rose follows it. A bearing taken of
// a watch lying still still wanders a little, and a rose that follows every
// reading quivers with it; a degree is below what anyone reads off the face,
// so holding still inside it costs the reading nothing.
constexpr float    kSteadyBandDeg   = 1.0f;

/// The shorter way round between two bearings, degrees in [0, 180].
float apartDeg(float a, float b)
{
    float apart = std::fabs(a - b);

    if (apart > 180.0f) {
        apart = 360.0f - apart;
    }

    return apart;
}
} // namespace

SettingsCompassView::SettingsCompassView()
    : mFailedCb(this, &SettingsCompassView::onFailedExpired)
{
}

void SettingsCompassView::setupScreen()
{
    SettingsCompassViewBase::setupScreen();

    title.set(T_TEXT_COMPASS_UC);

    mFailedTimer.setDuration(kFailedTicks);
    mFailedTimer.setCallback(mFailedCb);

    // Opened with the screen so R1 has a current reading to put up.
    presenter->magFeedStart();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::WHITE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);

    setMode(Mode::Resting);
}

void SettingsCompassView::tearDownScreen()
{
    // Also ends a run this screen started.
    presenter->magFeedStop();
    mFailedTimer.stop();

    SettingsCompassViewBase::tearDownScreen();
}

void SettingsCompassView::setMode(Mode mode)
{
    mMode = mode;

    if (mode == Mode::Failed) {
        touchgfx::Unicode::snprintf(hintBuffer, HINT_SIZE, "%s",
                touchgfx::TypedText(T_TEXT_COMPASS_RETRY).getText());
        mFailedTimer.start();
    } else {
        mFailedTimer.stop();

        if (mode == Mode::Resting) {
            touchgfx::Unicode::snprintf(hintBuffer, HINT_SIZE, "%s",
                    touchgfx::TypedText(T_TEXT_COMPASS_HINT).getText());
        }
    }

    applyVisibility();
}

void SettingsCompassView::applyVisibility()
{
    const bool running  = (mMode == Mode::Running);
    const bool resting  = (mMode == Mode::Resting);
    const bool bearing  = (mMode == Mode::Bearing);

    // Only on a bearing, and only once there is a calibration behind it.
    const bool showRose = bearing && mHaveFix;

    if (showRose) {
        // Turned to the newest sample before shown, so no stale heading
        // flashes up.
        rose.setHeading(mHeading);

        rose.setSweep(kFullSweep);
    }

    azimuth.setVisible(bearing);

    // The hint line also carries the failure notice.
    hint.setVisible(resting || (mMode == Mode::Failed));

    percent.setVisible(running);

    // The ring is up under the marks, and alone as a run fills it.
    rose.setVisible(showRose || running);
    rose.showMarks(showRose);

    // A button is lit, glyph and rail together, only while it does something.
    iconR1.setVisible(resting);
    buttons.setR1(resting ? Buttons::AMBER : Buttons::NONE);

    iconL2.setVisible(resting || bearing);
    buttons.setL2((resting || bearing) ? Buttons::WHITE : Buttons::NONE);

    azimuth.invalidate();
    rose.invalidate();
    hint.invalidate();
    percent.invalidate();
    iconR1.invalidate();
    iconL2.invalidate();
}

void SettingsCompassView::onIdleTimeout()
{
    // Only from rest: a run has to finish, and a bearing on screen is being
    // read, so it stays up until a key takes it down.
    if (mMode != Mode::Resting) {
        // Declined, so restart the countdown, which fires only once.
        presenter->resetIdleTimer();

        return;
    }

    application().gotoHomeScreenNoTransition();
}

void SettingsCompassView::showAzimuth(float degrees, bool bearing,
                                      float faceTiltDeg)
{
    // Nothing is drawn during a run, but the bearing is still recorded so the
    // one shown after it is current.
    const bool running = (mMode == Mode::Running);

    // A bearing this screen cannot show is shown as none. The tilt means
    // something only alongside a bearing, so without one the face keeps the
    // side of the band it was last seen on.
    if (bearing) {
        const float limit = mFaceReadable ? kHideFaceTiltDeg : kShowFaceTiltDeg;

        mFaceReadable = (faceTiltDeg <= limit);
    }

    const bool valid = bearing && mFaceReadable;

    if (valid) {
        // The first bearing is taken as it comes; after that the reading has
        // to leave the band before anything moves, and then it is followed
        // exactly. A watch actually being turned leaves it every reading, so
        // nothing is held back from a turn.
        if (!mShown || (apartDeg(degrees, mHeading) > kSteadyBandDeg)) {
            mShown   = true;
            mHeading = degrees;
        }

        touchgfx::Unicode::snprintf(azimuthBuffer, AZIMUTH_SIZE, "%d",
                static_cast<int>(mHeading + 0.5f) % 360);

        if (!running && rose.isVisible()) {
            rose.setHeading(mHeading);
        }
    } else {
        // Two dashes rather than a zero: zero is a bearing, due north.
        touchgfx::Unicode::snprintf(azimuthBuffer, AZIMUTH_SIZE, "--");

        // Whatever comes back is a fresh start, not a wander from this one.
        mShown = false;
    }

    // Kept up to date while hidden, so R1 puts up a current reading.
    if (mMode == Mode::Bearing) {
        azimuth.invalidate();
    }

    if (valid != mHaveFix) {
        mHaveFix = valid;

        // During a run the edge is applied when the run ends.
        if (!running) {
            applyVisibility();
        }
    }
}

void SettingsCompassView::showProgress(uint8_t percentDone)
{
    if (mMode != Mode::Running) {
        return;
    }

    rose.setSweep((static_cast<float>(percentDone) * kFullSweep) / 100.0f);

    // The sign comes from the text database rather than the format string:
    // Unicode::snprintf takes %c %s %u %i %d %o %x and nothing else.
    touchgfx::Unicode::snprintf(percentBuffer, PERCENT_SIZE, "%u%s",
            static_cast<unsigned>(percentDone),
            touchgfx::TypedText(T_TEXT_PERCENT).getText());
    percent.invalidate();
}

void SettingsCompassView::showDone(bool completed, bool stored)
{
    // Only an answer to a run the screen is waiting on; a second answer to
    // the same run is ignored.
    if (mMode != Mode::Running) {
        return;
    }

    if (!completed) {
        // Stopped or never started: not a failure, so no notice.
        setMode(Mode::Resting);

        return;
    }

    // A stored fit goes back to the now corrected bearing. A run with nothing
    // to keep says so, or success and failure would look the same.
    setMode(stored ? Mode::Bearing : Mode::Failed);
}

void SettingsCompassView::onFailedExpired()
{
    // Resting rather than the bearing: a run usually fails for want of a fix,
    // and without one the bearing is only a pair of dashes.
    setMode(Mode::Resting);
}

void SettingsCompassView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::R1) {
        if (mMode == Mode::Resting) {
            setMode(Mode::Bearing);
        }
        return;
    }

    if (key == Gui::Config::Button::L2) {
        if ((mMode == Mode::Resting) || (mMode == Mode::Bearing)) {
            touchgfx::Unicode::snprintf(percentBuffer, PERCENT_SIZE, "0%s",
                    touchgfx::TypedText(T_TEXT_PERCENT).getText());
            rose.setSweep(0.0f);
            setMode(Mode::Running);

            presenter->magCalStart(kRunSeconds);
        }
        return;
    }

    if (key == Gui::Config::Button::R2) {
        if (mMode == Mode::Running) {
            // Abort; answered with the done event a finished run sends.
            presenter->magCalCancel();
            return;
        }

        application().gotoSettingsSensorsScreenNoTransition();
        return;
    }
}
