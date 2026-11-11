#ifndef SETTINGSSOUNDSPRESENTER_HPP
#define SETTINGSSOUNDSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsSoundsView;

class SettingsSoundsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsSoundsPresenter(SettingsSoundsView& v);

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

    virtual ~SettingsSoundsPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void saveButtons(bool state);
    void saveMsgCalls(bool state);
    void saveAlerts(bool state);

private:
    SettingsSoundsPresenter();

    SettingsSoundsView& view;
};

#endif // SETTINGSSOUNDSPRESENTER_HPP
