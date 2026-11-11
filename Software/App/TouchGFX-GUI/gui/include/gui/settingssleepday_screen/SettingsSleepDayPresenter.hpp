#ifndef SETTINGSSLEEPDAYPRESENTER_HPP
#define SETTINGSSLEEPDAYPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <gui/common/GuiConfig.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsSleepDayView;

class SettingsSleepDayPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsSleepDayPresenter(SettingsSleepDayView& v);

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

    virtual ~SettingsSleepDayPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    /**
     * @brief Save an edited window.
     * @param day: the day that was edited, 0 for Monday.
     * @param applyTo: which days take the window.
     */
    void save(uint8_t day, uint16_t startMin, uint16_t endMin,
              Gui::Config::Menu::Top::Settings::Backlight::Sleep::ApplyTo applyTo);

private:
    SettingsSleepDayPresenter();

    SettingsSleepDayView& view;
};

#endif // SETTINGSSLEEPDAYPRESENTER_HPP
