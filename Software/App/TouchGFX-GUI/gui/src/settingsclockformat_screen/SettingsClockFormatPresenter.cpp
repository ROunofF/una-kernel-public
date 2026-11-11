#include <gui/settingsclockformat_screen/SettingsClockFormatView.hpp>
#include <gui/settingsclockformat_screen/SettingsClockFormatPresenter.hpp>

SettingsClockFormatPresenter::SettingsClockFormatPresenter(SettingsClockFormatView& v)
    : view(v)
{

}

void SettingsClockFormatPresenter::activate()
{
    // The shared picker edits either the time format or the date order.
    const bool dateMode = model->isClockPickerDate();
    view.configure(dateMode);
    view.setSelection(dateMode
        ? static_cast<int16_t>(model->getLocalSettings().clock.dateOrder)
        : static_cast<int16_t>(model->getLocalSettings().clock.format));

    // Reset idle timer
    model->resetIdleTimer();
}

void SettingsClockFormatPresenter::deactivate()
{

}

void SettingsClockFormatPresenter::saveFormat(WatchSettingsLocal::Clock::Format format)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.clock.format = format;
    model->saveLocalSettings(sett);
}

void SettingsClockFormatPresenter::saveDateOrder(WatchSettingsLocal::Clock::DateOrder order)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.clock.dateOrder = order;
    model->saveLocalSettings(sett);
}
