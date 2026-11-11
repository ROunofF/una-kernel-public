#ifndef SETTINGSGPSASSISTPRESENTER_HPP
#define SETTINGSGPSASSISTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsGpsAssistView;

class SettingsGpsAssistPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsGpsAssistPresenter(SettingsGpsAssistView& v);

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

    virtual ~SettingsGpsAssistPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }
    virtual void onGpsEpoTime(uint32_t h) override;
    virtual void onGpsEpoSyncState(bool state) override;

private:
    SettingsGpsAssistPresenter();

    SettingsGpsAssistView& view;
};

#endif // SETTINGSGPSASSISTPRESENTER_HPP
