#ifndef SETTINGSBACKLIGHTTIMEOUTVIEW_HPP
#define SETTINGSBACKLIGHTTIMEOUTVIEW_HPP

#include <gui_generated/settingsbacklighttimeout_screen/SettingsBacklightTimeoutViewBase.hpp>
#include <gui/settingsbacklighttimeout_screen/SettingsBacklightTimeoutPresenter.hpp>

/**
 * @brief Settings > Backlight > Timeout: pick 5, 10 or 15 seconds.
 */
class SettingsBacklightTimeoutView : public SettingsBacklightTimeoutViewBase
{
public:
    SettingsBacklightTimeoutView();
    virtual ~SettingsBacklightTimeoutView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /// Select the entry for a timeout in seconds.
    void setTimeout(uint8_t seconds);

protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSBACKLIGHTTIMEOUTVIEW_HPP
