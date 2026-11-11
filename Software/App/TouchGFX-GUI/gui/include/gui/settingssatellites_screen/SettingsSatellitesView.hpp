#ifndef SETTINGSSATELLITESVIEW_HPP
#define SETTINGSSATELLITESVIEW_HPP

#include <gui_generated/settingssatellites_screen/SettingsSatellitesViewBase.hpp>
#include <gui/settingssatellites_screen/SettingsSatellitesPresenter.hpp>

class SettingsSatellitesView : public SettingsSatellitesViewBase
{
public:
    SettingsSatellitesView();
    virtual ~SettingsSatellitesView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode);

protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSSATELLITESVIEW_HPP
