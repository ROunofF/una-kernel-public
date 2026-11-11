#ifndef SETTINGSGPSASSISTVIEW_HPP
#define SETTINGSGPSASSISTVIEW_HPP

#include <gui_generated/settingsgpsassist_screen/SettingsGpsAssistViewBase.hpp>
#include <gui/settingsgpsassist_screen/SettingsGpsAssistPresenter.hpp>

class SettingsGpsAssistView : public SettingsGpsAssistViewBase
{
public:
    SettingsGpsAssistView();
    virtual ~SettingsGpsAssistView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setEpoTimeH(uint32_t h);
    void setEpoSync();
protected:

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSGPSASSISTVIEW_HPP
