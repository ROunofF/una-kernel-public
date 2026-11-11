#include <gui/topmenu_screen/TopMenuView.hpp>
#include <gui/topmenu_screen/TopMenuPresenter.hpp>

TopMenuPresenter::TopMenuPresenter(TopMenuView& v)
    : view(v)
{

}

void TopMenuPresenter::activate()
{
    // Set current menu position
    view.setPositionId(model->getPosMenuTop());

    // Reset nested menu position
    model->setPosMenuTopSettings(Gui::Config::Menu::Top::Settings::ID_DEFAULT);

    // Set alert mute and sleep state
    view.setAlertMute(model->getLocalSettings().alertMutedFlag);
    view.setSleep(model->getLocalSettings().sleep.active);

    // Reset idle timer
    model->resetIdleTimer();

    // L1 and L2 held run the list on, so the hold is spent here -- without this
    // letting go of L2 would also drop the user into the media controls.
    model->claimL2Hold(true);

    // What is already known, before asking for it again. A request cannot be
    // answered before this screen's first frame, so without this the menu draws
    // the fixed entries, then the apps, then their icons -- three states the
    // user sees it pass through. Applied after setPositionId() above, which
    // updAppList() reads to place the selection.
    const Model::AppCache* cache = model->appCache(App::Type::kUtility);
    if (cache != nullptr && cache->list != nullptr) {
        view.updAppList(App::Type::kUtility, cache->list);

        if (cache->iconCount > 0) {
            view.updAppIcons(App::Type::kUtility, cache->icons, cache->iconCount);
        }
    }

    // Request app list for top menu
    model->appRequestList(App::Type::kUtility);
}

void TopMenuPresenter::deactivate()
{
    model->claimL2Hold(false);

    view.clearIcons(); // Clear dynamic icons
    model->setPosMenuTop(view.getPositionId());
}

void TopMenuPresenter::onAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>> &list)
{
    if (type != App::Type::kUtility) {
        return;
    }

    view.updAppList(type, list);
}

void TopMenuPresenter::onAppIcons(App::Type type, const App::IconRef *icons, uint8_t count)
{
    // Only this menu's own type. Another type's window is a different set of
    // apps, and handing it over would have the view treat every icon it is
    // holding as gone -- blanking the arc until the selection next moved.
    if (type != App::Type::kUtility) {
        return;
    }

    view.updAppIcons(type, icons, count);
}

void TopMenuPresenter::requestIconWindow(const uint64_t *appIds, uint8_t count)
{
    model->appRequestIconWindow(App::Type::kUtility, appIds, count);
}

void TopMenuPresenter::saveMuteAlerts(bool muted)
{
    WatchSettingsLocal sett = model->getLocalSettings();
    sett.alertMutedFlag = muted;
    model->saveLocalSettings(sett);
}

void TopMenuPresenter::setSleep(bool on)
{
    model->setSleepMode(on);
}

void TopMenuPresenter::onLocalSettings(const WatchSettingsLocal &sett)
{
    view.setAlertMute(sett.alertMutedFlag);
    view.setSleep(sett.sleep.active);
}

bool TopMenuPresenter::appStart(uint32_t pid)
{
    return model->appStart(pid, Gui::Config::AppLaunchOrigin::TOP_MENU);
}
