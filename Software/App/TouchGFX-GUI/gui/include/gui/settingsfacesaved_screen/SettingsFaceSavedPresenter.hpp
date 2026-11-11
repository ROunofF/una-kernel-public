#ifndef SETTINGSFACESAVEDPRESENTER_HPP
#define SETTINGSFACESAVEDPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SettingsFaceSavedView;

class SettingsFaceSavedPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsFaceSavedPresenter(SettingsFaceSavedView& v);

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

    virtual ~SettingsFaceSavedPresenter() {}

private:
    SettingsFaceSavedPresenter();

    SettingsFaceSavedView& view;
};

#endif // SETTINGSFACESAVEDPRESENTER_HPP
