#ifndef SETTINGSCOMPASSVIEW_HPP
#define SETTINGSCOMPASSVIEW_HPP

#include <gui_generated/settingscompass_screen/SettingsCompassViewBase.hpp>
#include <gui/settingscompass_screen/SettingsCompassPresenter.hpp>
#include <gui/containers/CountdownTimer.hpp>

class SettingsCompassView : public SettingsCompassViewBase
{
public:
    SettingsCompassView();
    virtual ~SettingsCompassView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    // Pushed from the presenter.
    void onIdleTimeout();
    void showAzimuth(float degrees, bool bearing, float faceTiltDeg);
    void showProgress(uint8_t percent);
    void showDone(bool completed, bool stored);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    // The screen opens on the hint alone; R1 asks for the bearing.
    enum class Mode {
        Resting,    // the hint alone
        Bearing,    // the reading, and the rose behind it once calibrated
        Running,    // the ring filling, the count in the middle
        Failed,     // a run that finished with nothing worth keeping
    };

    Mode     mMode { Mode::Resting };

    // Whether the last reading was a bearing at all; the rose is only up then.
    bool     mHaveFix { false };
    float    mHeading { 0.0f };    // last bearing shown, kept for when the rose returns
    bool     mShown { false };     // whether mHeading is a bearing that was shown

    // Whether the face is held near enough to the sky for a bearing to be
    // shown on it, with the band between the two tilt limits keeping the last
    // answer.
    bool     mFaceReadable { false };

    void setMode(Mode mode);
    void applyVisibility();

    // Takes the failure notice back down to resting on its own.
    void onFailedExpired();

    CountdownTimer                          mFailedTimer;
    touchgfx::Callback<SettingsCompassView> mFailedCb;
};

#endif // SETTINGSCOMPASSVIEW_HPP
