#include <gui/trackingmenu_screen/TrackingMenuView.hpp>
#include <gui/trackingmenu_screen/TrackingMenuPresenter.hpp>

TrackingMenuPresenter::TrackingMenuPresenter(TrackingMenuView& v)
    : view(v)
{

}

void TrackingMenuPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuTracking());

    // Reset idle timer
    model->resetIdleTimer();

    // Set alert mute state
    view.setAlertMute(model->getLocalSettings().alertMutedFlag);

    // What is already known, before asking for it again: a request cannot be
    // answered before this screen's first frame, so without this the wheel
    // draws its rows and then their icons -- two states the user sees it pass
    // through. Applied after setPositionId() above, which updAppList() reads to
    // place the selection.
    const Model::AppCache* cache = model->appCache(App::Type::kActivity);
    if (cache != nullptr && cache->list != nullptr) {
        view.updAppList(App::Type::kActivity, cache->list);

        if (cache->iconCount > 0) {
            view.updAppIcons(App::Type::kActivity, cache->icons, cache->iconCount);
        }
    }

    // Request app list for tracking menu
    model->appRequestList(App::Type::kActivity);
}

void TrackingMenuPresenter::deactivate()
{
    model->setPosMenuTracking(view.getPositionId());
}

void TrackingMenuPresenter::onAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>>& list)
{
    if (type != App::Type::kActivity) {
        return;
    }

    view.updAppList(type, list);
}

void TrackingMenuPresenter::onAppIcons(App::Type type, const App::IconRef *icons, uint8_t count)
{
    // See TopMenuPresenter: another type's window would blank this one's rows.
    if (type != App::Type::kActivity) {
        return;
    }

    view.updAppIcons(type, icons, count);
}

void TrackingMenuPresenter::requestIconWindow(const uint64_t *appIds, uint8_t count)
{
    model->appRequestIconWindow(App::Type::kActivity, appIds, count);
}

bool TrackingMenuPresenter::appStart(uint32_t pid)
{
    return model->appStart(pid, Gui::Config::AppLaunchOrigin::TRACKING_MENU);
}
