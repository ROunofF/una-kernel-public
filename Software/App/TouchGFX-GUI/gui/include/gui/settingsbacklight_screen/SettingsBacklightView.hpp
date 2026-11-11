#ifndef SETTINGSBACKLIGHTVIEW_HPP
#define SETTINGSBACKLIGHTVIEW_HPP

#include <gui_generated/settingsbacklight_screen/SettingsBacklightViewBase.hpp>
#include <gui/settingsbacklight_screen/SettingsBacklightPresenter.hpp>

/**
 * @brief Settings > Backlight: the lift-to-wake gesture, the timeout, and the
 *        way into sleep mode.
 */
class SettingsBacklightView : public SettingsBacklightViewBase
{
public:
    SettingsBacklightView();
    virtual ~SettingsBacklightView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    /// Show whether lift-to-wake is on, on the Gesture toggle.
    void setGesture(bool on);
    /// Show the timeout, in seconds, as the Timeout entry's tip.
    void setTimeout(uint8_t seconds);
    /// Show whether the sleep schedule is on as the Sleep Mode entry's tip.
    void setSchedule(bool on);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    bool mGesture = true;
};

#endif // SETTINGSBACKLIGHTVIEW_HPP
