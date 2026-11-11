#ifndef TRACKINGMENUPRESENTER_HPP
#define TRACKINGMENUPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TrackingMenuView;

class TrackingMenuPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TrackingMenuPresenter(TrackingMenuView& v);

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

    virtual ~TrackingMenuPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    virtual void onAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>>& list) override;
    virtual void onAppIcons(App::Type type, const App::IconRef *icons, uint8_t count) override;

    /// Say which apps' icons the wheel is showing. See Model::appRequestIconWindow().
    void requestIconWindow(const uint64_t *appIds, uint8_t count);

    bool appStart(uint32_t pid);

private:
    TrackingMenuPresenter();

    TrackingMenuView& view;
};

#endif // TRACKINGMENUPRESENTER_HPP
