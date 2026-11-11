#ifndef SETTINGSSOUNDSVIEW_HPP
#define SETTINGSSOUNDSVIEW_HPP

#include <gui_generated/settingssounds_screen/SettingsSoundsViewBase.hpp>
#include <gui/settingssounds_screen/SettingsSoundsPresenter.hpp>

class SettingsSoundsView : public SettingsSoundsViewBase
{
public:
    SettingsSoundsView();
    virtual ~SettingsSoundsView() {}
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

#endif // SETTINGSSOUNDSVIEW_HPP
