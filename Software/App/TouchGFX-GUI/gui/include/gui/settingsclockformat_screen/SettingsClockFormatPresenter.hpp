#ifndef SETTINGSCLOCKFORMATPRESENTER_HPP
#define SETTINGSCLOCKFORMATPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsClockFormatView;

class SettingsClockFormatPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsClockFormatPresenter(SettingsClockFormatView& v);

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

    virtual ~SettingsClockFormatPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    void saveFormat(WatchSettingsLocal::Clock::Format format);
    void saveDateOrder(WatchSettingsLocal::Clock::DateOrder order);

private:
    SettingsClockFormatPresenter();

    SettingsClockFormatView& view;
};

#endif // SETTINGSCLOCKFORMATPRESENTER_HPP
