#ifndef SETTINGSBACKLIGHTPRESENTER_HPP
#define SETTINGSBACKLIGHTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsBacklightView;

class SettingsBacklightPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsBacklightPresenter(SettingsBacklightView& v);

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

    virtual ~SettingsBacklightPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    /// Turn lift-to-wake on or off.
    void saveGesture(bool on);

private:
    SettingsBacklightPresenter();

    SettingsBacklightView& view;
};

#endif // SETTINGSBACKLIGHTPRESENTER_HPP
