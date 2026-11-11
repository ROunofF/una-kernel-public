#ifndef SETTINGSCLOCKFORMATVIEW_HPP
#define SETTINGSCLOCKFORMATVIEW_HPP

#include <gui_generated/settingsclockformat_screen/SettingsClockFormatViewBase.hpp>
#include <gui/settingsclockformat_screen/SettingsClockFormatPresenter.hpp>

class SettingsClockFormatView : public SettingsClockFormatViewBase
{
public:
    SettingsClockFormatView();
    virtual ~SettingsClockFormatView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /** @brief Build the picker for the time format (false) or the date order (true). */
    void configure(bool dateMode);
    /** @brief Highlight the option at @p index (enum value of the active mode). */
    void setSelection(int16_t index);

protected:
    /// true while the picker edits the date order rather than the time format.
    bool mDateMode = false;

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSCLOCKFORMATVIEW_HPP
