#ifndef SETTINGSVIEW_HPP
#define SETTINGSVIEW_HPP

#include <gui_generated/settings_screen/SettingsViewBase.hpp>
#include <gui/settings_screen/SettingsPresenter.hpp>

class SettingsView : public SettingsViewBase
{
public:
    SettingsView();
    virtual ~SettingsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    void bonded(bool bonded);
    void useBleFCC(bool enable);
protected:
    bool mBonded = false;
    bool mUseBleFCC = false;

    virtual void handleKeyEvent(uint8_t key) override;
    void update();
};

#endif // SETTINGSVIEW_HPP
