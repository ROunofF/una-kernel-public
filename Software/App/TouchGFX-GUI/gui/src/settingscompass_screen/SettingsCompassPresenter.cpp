#include <gui/settingscompass_screen/SettingsCompassView.hpp>
#include <gui/settingscompass_screen/SettingsCompassPresenter.hpp>

SettingsCompassPresenter::SettingsCompassPresenter(SettingsCompassView& v)
    : view(v)
{
}

void SettingsCompassPresenter::activate()
{
    // The countdown is shared across screens and already part spent.
    model->resetIdleTimer();
}

void SettingsCompassPresenter::deactivate()
{
}

void SettingsCompassPresenter::magFeedStart()
{
    model->magFeedStart();
}

void SettingsCompassPresenter::magFeedStop()
{
    model->magFeedStop();
}

void SettingsCompassPresenter::magCalStart(uint32_t seconds)
{
    model->magCalStart(seconds);
}

void SettingsCompassPresenter::magCalCancel()
{
    model->magCalCancel();
}

void SettingsCompassPresenter::resetIdleTimer()
{
    model->resetIdleTimer();
}

void SettingsCompassPresenter::onIdleTimeout()
{
    view.onIdleTimeout();
}

void SettingsCompassPresenter::onMagAzimuth(float degrees, bool valid,
                                            float faceTiltDeg)
{
    view.showAzimuth(degrees, valid, faceTiltDeg);
}

void SettingsCompassPresenter::onMagCalProgress(uint8_t percent)
{
    view.showProgress(percent);
}

void SettingsCompassPresenter::onMagCalDone(bool completed, bool stored)
{
    view.showDone(completed, stored);
}
