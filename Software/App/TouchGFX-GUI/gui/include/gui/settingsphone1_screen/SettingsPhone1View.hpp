#ifndef SETTINGSPHONE1VIEW_HPP
#define SETTINGSPHONE1VIEW_HPP

#include <gui_generated/settingsphone1_screen/SettingsPhone1ViewBase.hpp>
#include <gui/settingsphone1_screen/SettingsPhone1Presenter.hpp>

class SettingsPhone1View : public SettingsPhone1ViewBase
{
public:
    SettingsPhone1View();
    virtual ~SettingsPhone1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSPHONE1VIEW_HPP
