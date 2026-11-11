#ifndef SETTINGSVIBRATIONPRESENTER_HPP
#define SETTINGSVIBRATIONPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsVibrationView;

class SettingsVibrationPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsVibrationPresenter(SettingsVibrationView& v);

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

    virtual ~SettingsVibrationPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void saveButtons(bool state);
    void saveMsgCalls(bool state);
    void saveAlerts(bool state);

private:
    SettingsVibrationPresenter();

    SettingsVibrationView& view;
};

#endif // SETTINGSVIBRATIONPRESENTER_HPP
