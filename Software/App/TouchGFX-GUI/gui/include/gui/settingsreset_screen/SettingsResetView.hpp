#ifndef SETTINGSRESETVIEW_HPP
#define SETTINGSRESETVIEW_HPP

#include <gui_generated/settingsreset_screen/SettingsResetViewBase.hpp>
#include <gui/settingsreset_screen/SettingsResetPresenter.hpp>

class SettingsResetView : public SettingsResetViewBase
{
public:
    SettingsResetView();
    virtual ~SettingsResetView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSRESETVIEW_HPP
