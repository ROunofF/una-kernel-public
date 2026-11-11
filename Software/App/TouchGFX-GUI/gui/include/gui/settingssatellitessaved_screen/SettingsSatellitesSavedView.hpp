#ifndef SETTINGSSATELLITESSAVEDVIEW_HPP
#define SETTINGSSATELLITESSAVEDVIEW_HPP

#include <gui_generated/settingssatellitessaved_screen/SettingsSatellitesSavedViewBase.hpp>
#include <gui/settingssatellitessaved_screen/SettingsSatellitesSavedPresenter.hpp>

class SettingsSatellitesSavedView : public SettingsSatellitesSavedViewBase
{
public:
    SettingsSatellitesSavedView();
    virtual ~SettingsSatellitesSavedView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode);
protected:
    uint16_t mCounter = Gui::Config::kSavedScreenTimeout;
    virtual void handleTickEvent() override;
};

#endif // SETTINGSSATELLITESSAVEDVIEW_HPP
