#ifndef SETTINGSGNSSVIEW_HPP
#define SETTINGSGNSSVIEW_HPP

#include <gui_generated/settingsgnss_screen/SettingsGNSSViewBase.hpp>
#include <gui/settingsgnss_screen/SettingsGNSSPresenter.hpp>

class SettingsGNSSView : public SettingsGNSSViewBase
{
public:
    SettingsGNSSView();
    virtual ~SettingsGNSSView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    void setGpsPowerMode(WatchSettingsLocal::GpsPowerMode mode);

protected:
    WatchSettingsLocal::GpsPowerMode mMode{};
    void update();
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SETTINGSGNSSVIEW_HPP
