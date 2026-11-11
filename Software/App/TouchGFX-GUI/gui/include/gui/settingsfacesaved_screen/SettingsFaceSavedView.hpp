#ifndef SETTINGSFACESAVEDVIEW_HPP
#define SETTINGSFACESAVEDVIEW_HPP

#include <gui_generated/settingsfacesaved_screen/SettingsFaceSavedViewBase.hpp>
#include <gui/settingsfacesaved_screen/SettingsFaceSavedPresenter.hpp>

class SettingsFaceSavedView : public SettingsFaceSavedViewBase
{
public:
    SettingsFaceSavedView();
    virtual ~SettingsFaceSavedView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    uint16_t mCounter = Gui::Config::kSavedScreenTimeout;
    virtual void handleTickEvent() override;
};

#endif // SETTINGSFACESAVEDVIEW_HPP
