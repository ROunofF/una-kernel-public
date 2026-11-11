#ifndef SETTINGSVIBRATIONVIEW_HPP
#define SETTINGSVIBRATIONVIEW_HPP

#include <gui_generated/settingsvibration_screen/SettingsVibrationViewBase.hpp>
#include <gui/settingsvibration_screen/SettingsVibrationPresenter.hpp>

class SettingsVibrationView : public SettingsVibrationViewBase
{
public:
    SettingsVibrationView();
    virtual ~SettingsVibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setButtons(bool state);
    void setMsgCalls(bool state);
    void setAlerts(bool state);

protected:
    bool mButtons {};
    bool mMsgCalls {};
    bool mAlerts {};

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSVIBRATIONVIEW_HPP
