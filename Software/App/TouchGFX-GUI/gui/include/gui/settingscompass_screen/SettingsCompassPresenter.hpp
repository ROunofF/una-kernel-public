#ifndef SETTINGSCOMPASSPRESENTER_HPP
#define SETTINGSCOMPASSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsCompassView;

class SettingsCompassPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsCompassPresenter(SettingsCompassView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~SettingsCompassPresenter() {}

    // Commands, straight through to the backend.
    void magFeedStart();
    void magFeedStop();
    void magCalStart(uint32_t seconds);
    void magCalCancel();

    /// Start the screen countdown again. It fires once, so a screen that
    /// declines a timeout has to restart it.
    void resetIdleTimer();

    //// ModelListener - pushed state.
    void onIdleTimeout() override;
    void onMagAzimuth(float degrees, bool valid, float faceTiltDeg) override;
    void onMagCalProgress(uint8_t percent) override;
    void onMagCalDone(bool completed, bool stored) override;

private:
    SettingsCompassPresenter();

    SettingsCompassView& view;
};

#endif // SETTINGSCOMPASSPRESENTER_HPP
