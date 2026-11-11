#ifndef SETTINGSSLEEPPRESENTER_HPP
#define SETTINGSSLEEPPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsSleepView;

class SettingsSleepPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsSleepPresenter(SettingsSleepView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~SettingsSleepPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    virtual void onLocalSettings(const WatchSettingsLocal &sett) override;

    void saveSchedule(bool on);

    /// Choose the day the window editor opens on, 0 for Monday.
    void editDay(uint8_t day) { model->setSleepEditDay(day); }

private:
    SettingsSleepPresenter();

    SettingsSleepView& view;
};

#endif // SETTINGSSLEEPPRESENTER_HPP
