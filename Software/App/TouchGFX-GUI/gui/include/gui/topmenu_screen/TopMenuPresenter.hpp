#ifndef TOPMENUPRESENTER_HPP
#define TOPMENUPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TopMenuView;

class TopMenuPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TopMenuPresenter(TopMenuView& v);

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

    virtual ~TopMenuPresenter() {}

    virtual void onIdleTimeout() override { model->application().gotoHomeScreenNoTransition(); }

    virtual void onAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>> &list) override;
    virtual void onAppIcons(App::Type type, const App::IconRef *icons, uint8_t count) override;

    /// Sleep mode changes alerts as well as itself, and the backend can change
    /// either while the menu is open (a scheduled start, the answer to a toggle).
    virtual void onLocalSettings(const WatchSettingsLocal &sett) override;

    /// Say which apps' icons the arc is showing. See Model::appRequestIconWindow().
    void requestIconWindow(const uint64_t *appIds, uint8_t count);

    void saveMuteAlerts(bool muted);
    void setSleep(bool on);

    bool appStart(uint32_t pid);

private:
    TopMenuPresenter();

    TopMenuView& view;
};

#endif // TOPMENUPRESENTER_HPP
