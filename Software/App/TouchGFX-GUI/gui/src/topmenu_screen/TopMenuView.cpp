#include <gui/topmenu_screen/TopMenuView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>

static_assert(Gui::Config::Menu::Top::kMaxOrderItems == MenuTop::kMaxItems,
              "the order and the arc must agree on how many items there can be");

TopMenuView::TopMenuView()
{

}

void TopMenuView::setupScreen()
{
    TopMenuViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);

    // Only the fixed entries until the app list lands a tick from now, so the
    // order is short by Alarm, Stopwatch and Timer, and Settings is third
    // rather than fourth. rebuildItems() sorts that out when the list arrives.
    menu.selectItem(rebuildItems(captureSelection()));
}

void TopMenuView::configFixed(int16_t id, int16_t fixedId)
{
    switch (fixedId) {
        case Gui::Config::Menu::Top::ID_POWEROFF:
            menu.config(id,
                touchgfx::TypedText(T_TEXT_POWER_OFF).getText(),
                BITMAP_TOPMENU_POWERSMALL_ID, BITMAP_TOPMENU_POWERBIG_ID);
            break;

        case Gui::Config::Menu::Top::ID_SETTINGS:
            menu.config(id,
                touchgfx::TypedText(T_TEXT_SETTINGS).getText(),
                BITMAP_TOPMENU_SETTINGSMALL_ID, BITMAP_TOPMENU_SETTING_BIG_ID);
            break;

        case Gui::Config::Menu::Top::ID_ALERTS:
            menu.config(id,
                touchgfx::TypedText(T_TEXT_ALERTS_ON).getText(),
                BITMAP_TOPMENU_ALERTSMALL_ID, BITMAP_TOPMENU_ALERTBIG_ID,
                touchgfx::TypedText(T_TEXT_ALERTS_OFF).getText(),
                BITMAP_TOPMENU_NOALERTSMALL_ID, BITMAP_TOPMENU_NOALERTBIG_ID);
            break;

        // Unlike Alerts, the alternate is the "on" state: useAlt = sleeping.
        case Gui::Config::Menu::Top::ID_SLEEP:
            menu.config(id,
                touchgfx::TypedText(T_TEXT_SLEEP_OFF).getText(),
                BITMAP_TOPMENU_NOSLEEPSMALL_ID, BITMAP_TOPMENU_NOSLEEPBIG_ID,
                touchgfx::TypedText(T_TEXT_SLEEP_ON).getText(),
                BITMAP_TOPMENU_SLEEPSMALL_ID, BITMAP_TOPMENU_SLEEPBIG_ID);
            break;

        default:
            break;
    }
}

int16_t TopMenuView::appIndexForItem(int16_t id) const
{
    if (id < 0 || id >= mItemCount) {
        return -1;
    }
    return mItems[id].appIdx;
}

TopMenuView::Selection TopMenuView::captureSelection() const
{
    Selection keep;

    if (mRequestedId >= 0) {
        // A position the presenter restored. Only a position: it indexed the
        // menu as it stood when the screen was last open, and the table it
        // indexed no longer exists, so it cannot be turned into an identity.
        keep.rawId = mRequestedId;
        return keep;
    }

    const int16_t id = menu.getSelectedItem();
    if (id < 0 || id >= mItemCount) {
        return keep;
    }

    keep.fixed = mItems[id].fixed;

    const int16_t appIdx = mItems[id].appIdx;
    if (appIdx >= 0 && mAppList != nullptr
            && static_cast<size_t>(appIdx) < mAppList->size()) {
        keep.appId = (*mAppList)[appIdx].appId;
    }

    return keep;
}

int16_t TopMenuView::rebuildItems(const Selection &keep)
{
    mItemCount = Gui::Config::Menu::Top::buildOrder(
            mAppList.get(), Gui::Config::Menu::Top::kMaxOrderApps, mItems.data(),
            static_cast<int16_t>(mItems.size()));

    menu.setNumberOfItems(mItemCount);

    // Names now, icons when they arrive: the list carries no pixels, because
    // at a hundred apps it would be a quarter of a megabyte of them for the
    // seven the arc shows. Until then an item draws its label and no icon,
    // which is what configExternal() does with null buffers.
    for (int16_t id = 0; id < mItemCount; ++id) {
        const int16_t appIdx = mItems[id].appIdx;

        if (appIdx < 0) {
            configFixed(id, mItems[id].fixed);
            continue;
        }

        const auto& item = (*mAppList)[appIdx];
        Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(item.name),
                          mUserAppNames[appIdx].name, App::kNameLen);
        menu.configExternal(id, mUserAppNames[appIdx].name, nullptr, nullptr);
    }

    // config() has just reset useAlt on the toggle entries, and they may have
    // moved, so their states go back on here. The presenter says them once, on
    // the way in, before the app list has arrived.
    setFixedAlt(Gui::Config::Menu::Top::ID_ALERTS, mAlertsMuted);
    setFixedAlt(Gui::Config::Menu::Top::ID_SLEEP, mSleepActive);

    // Follow whatever was selected to wherever it has moved to.
    if (keep.appId != 0) {
        const int16_t id = itemForAppId(keep.appId);
        if (id >= 0) {
            return id;
        }
    }

    if (keep.fixed >= 0) {
        const int16_t id = Gui::Config::Menu::Top::itemForFixed(
                mItems.data(), mItemCount, keep.fixed);
        if (id >= 0) {
            return id;
        }
    }

    if (keep.rawId >= 0) {
        // A restored position, applied as one. Clamped, because the menu can be
        // shorter than it was -- an app the position pointed at may be gone.
        return (keep.rawId < mItemCount)
             ? keep.rawId : static_cast<int16_t>(mItemCount - 1);
    }

    const int16_t settings = Gui::Config::Menu::Top::itemForFixed(
            mItems.data(), mItemCount, Gui::Config::Menu::Top::ID_SETTINGS);
    return (settings >= 0) ? settings : 0;
}

void TopMenuView::tearDownScreen()
{
    TopMenuViewBase::tearDownScreen();
}

void TopMenuView::setPositionId(uint16_t id)
{
    if (id == Gui::Config::Menu::Top::ID_DEFAULT) {
        // Not an item id, but "wherever Settings is" -- which is what the Home
        // screen asks for, and which item that is depends on whether Alarm is
        // installed. rebuildItems() falls back to Settings on its own.
        mRequestedId = -1;
        menu.selectItem(rebuildItems(captureSelection()));
        return;
    }

    // Only the fixed entries exist at this point, so a position pointing at an
    // app is held at the last of them; updAppList() re-applies it once the apps
    // are in.
    mRequestedId = static_cast<int16_t>(id);
    menu.selectItem(mRequestedId);
}

uint16_t TopMenuView::getPositionId()
{
    // While a request is outstanding the wheel only holds the fixed items, so
    // the live selection is a clamped version of it - report what was asked for
    // instead, or leaving the screen would persist the clamped position.
    if (mRequestedId >= 0) {
        return static_cast<uint16_t>(mRequestedId);
    }
    return menu.getSelectedItem();
}

void TopMenuView::setAlertMute(bool muted)
{
    mAlertsMuted = muted;
    setFixedAlt(Gui::Config::Menu::Top::ID_ALERTS, muted);
}

void TopMenuView::setSleep(bool active)
{
    mSleepActive = active;
    setFixedAlt(Gui::Config::Menu::Top::ID_SLEEP, active);
}

void TopMenuView::setFixedAlt(int16_t fixedId, bool alt)
{
    const int16_t id = Gui::Config::Menu::Top::itemForFixed(
            mItems.data(), mItemCount, fixedId);
    if (id >= 0) {
        menu.setAltIcon(id, alt);
    }
}

void TopMenuView::updAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>> &list)
{
    // The icons are not dropped here, and nothing releases them: the menu
    // points into the icon sets, which this list does not own, so replacing the
    // list leaves what is on screen valid. The loop below clears every app
    // item's buffers and applyIcons() puts back the ones still in hand, so a
    // list merely refreshed does not blink its icons off and on.
    // Read before the list is replaced: an app is remembered by its own id,
    // looked up in the list the current table was built from. Reading it after
    // would index the new list with an index into the old one, and the two
    // differ in order whenever a launch has changed the most-used sort.
    const Selection keep = captureSelection();

    mAppList = list;

    // The list lands a tick after the screen opens, so hold the position the
    // presenter restored - notably the app the user just backed out of -
    // instead of snapping back to the default item.
    const int16_t landOn = rebuildItems(keep);

    if (mAppList == nullptr || mAppList->empty()) {
        // No apps: only the fixed entries. Keep the request outstanding - a
        // position pointing at an app can only be applied once one exists.
        menu.selectItem(landOn);
        return;
    }

    // Applied below; a later refresh must go with the live selection rather
    // than drag the cursor back here.
    mRequestedId = -1;

    menu.selectItem(landOn);

    // Before asking for more: whatever is already in hand goes straight back
    // on, so the only items without an icon are the ones we have never had.
    applyIcons();

    requestIcons();
}

void TopMenuView::updAppIcons(App::Type type, const App::IconRef *icons, uint8_t count)
{
    (void)type;

    if (count > App::kIconWindowMax) {
        count = App::kIconWindowMax;
    }

    // Cleared one item at a time rather than by releasing the lot: a blanket
    // release hides every icon, the dot and the centre, and only the items that
    // happen to be in the new set get put back -- so an empty set, or one that
    // does not cover what is on screen, would leave the menu blank until the
    // next reply. Both ends of this are bounded by the window, so it costs the
    // same whatever the list length.
    for (uint8_t g = 0; g < mIconCount; ++g) {
        if (!mIcons[g]) {
            continue;
        }

        const uint64_t goingId = mIcons[g]->appId;

        bool stillHeld = false;
        for (uint8_t a = 0; a < count; ++a) {
            if (icons[a] && icons[a]->appId == goingId) {
                stillHeld = true;
                break;
            }
        }

        if (stillHeld) {
            continue;
        }

        const int16_t id = itemForAppId(goingId);
        const int16_t appIdx = appIndexForItem(id);
        if (appIdx >= 0) {
            menu.configExternal(id, mUserAppNames[appIdx].name, nullptr, nullptr);
        }
    }

    // Only now: the loop above had to unpoint the menu from these buffers
    // before the last reference to them went.
    for (uint8_t i = 0; i < App::kIconWindowMax; ++i) {
        mIcons[i] = (i < count) ? icons[i] : App::IconRef();
    }
    mIconCount = count;

    applyIcons();
}

int16_t TopMenuView::itemForAppId(uint64_t appId) const
{
    if (mAppList == nullptr) {
        return -1;
    }

    const int16_t maxItems = MIN(static_cast<int16_t>(mAppList->size()), kMaxUserApps);

    for (int16_t i = 0; i < maxItems; ++i) {
        if ((*mAppList)[i].appId == appId) {
            return Gui::Config::Menu::Top::itemForAppIndex(
                    mItems.data(), mItemCount, i);
        }
    }

    return -1;
}

void TopMenuView::applyIcons()
{
    if (mAppList == nullptr) {
        return;
    }

    // Only the arc's own handful, so this costs nothing whatever the list
    // length: an item with no icon yet keeps the null buffers it was given.
    for (uint8_t i = 0; i < mIconCount; ++i) {
        if (!mIcons[i]) {
            continue;
        }

        const int16_t id = itemForAppId(mIcons[i]->appId);
        const int16_t appIdx = appIndexForItem(id);
        if (appIdx < 0) {
            continue;   // arrived for a list we have already replaced
        }

        menu.configExternal(id, mUserAppNames[appIdx].name,
                            mIcons[i]->smallIcon.data(), mIcons[i]->icon.data());
    }
}

void TopMenuView::requestIcons()
{
    int16_t items[G2BEvent::UserAppIconWindow::kMax];
    const int16_t found = menu.getIconWindow(items,
            static_cast<int16_t>(G2BEvent::UserAppIconWindow::kMax));

    uint64_t appIds[G2BEvent::UserAppIconWindow::kMax];
    uint8_t count = 0;

    for (int16_t i = 0; i < found; ++i) {
        const int16_t appIdx = appIndexForItem(items[i]);

        // A fixed entry: its icons are bitmap-database entries, not app files.
        if (appIdx < 0 || mAppList == nullptr
                || static_cast<size_t>(appIdx) >= mAppList->size()) {
            continue;
        }

        appIds[count++] = (*mAppList)[appIdx].appId;
    }

    // Sent even when empty, which is how the loader is told it may let the
    // whole window go.
    presenter->requestIconWindow(appIds, count);
}

void TopMenuView::clearIcons()
{
    menu.releaseIcons();

    for (App::IconRef &ref : mIcons) {
        ref.reset();
    }
    mIconCount = 0;

    presenter->requestIconWindow(nullptr, 0);
}

bool TopMenuView::step(bool forward)
{
    mRequestedId = -1; // the user is driving now

    const int16_t before = menu.getSelectedItem();

    if (forward) {
        menu.selectNext();
    } else {
        menu.selectPrev();
    }

    if (menu.getSelectedItem() == before) {
        return false;  // held against an end of the list
    }

    requestIcons();    // the strip has moved: a different set is on the arc
    return true;
}

void TopMenuView::handleTickEvent()
{
    if (mHeldKey == 0) {
        return;
    }

    mHeldTicks++;

    if (mHeldTicks < kHoldDelay) {
        return;    // still could be a single step; nothing to do yet
    }

    const uint32_t running = mHeldTicks - kHoldDelay;
    const uint32_t period  = (running >= kHoldAccelAfter) ? kHoldPeriodFast : kHoldPeriod;

    if ((running % period) != 0) {
        return;
    }

    step(mHeldKey == Gui::Config::Button::L2_PRESS);
}

void TopMenuView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1_PRESS || key == Gui::Config::Button::L2_PRESS) {
        mHeldKey   = key;
        mHeldTicks = 0;

        // The step belongs to the press, not to the click that a short release
        // would turn into -- so the arc answers the button going down, and the
        // click is then ignored below rather than counted twice.
        step(key == Gui::Config::Button::L2_PRESS);
    }

    if (key == Gui::Config::Button::L1_RELEASE || key == Gui::Config::Button::L2_RELEASE) {
        mHeldKey   = 0;
        mHeldTicks = 0;
    }

    // Acted on only if the press that led here was not: a click follows every
    // short press, and stepping on both would move two for one push. If the
    // press never arrived, this is what still drives the menu.
    if (key == Gui::Config::Button::L1 && mHeldKey == 0) {
        step(false);
    }

    if (key == Gui::Config::Button::L2 && mHeldKey == 0) {
        step(true);
    }

    if (key == Gui::Config::Button::R1) {
        mRequestedId = -1; // the user acted on what is on screen
        const int16_t id = menu.getSelectedItem();
        const int16_t appIdx = appIndexForItem(id);
        const int16_t fixed = (id >= 0 && id < mItemCount) ? mItems[id].fixed : -1;

        switch (fixed) {
            case Gui::Config::Menu::Top::ID_POWEROFF:
                application().gotoTurnOffScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::ID_SETTINGS:
                application().gotoSettingsScreenNoTransition();
                break;
            case Gui::Config::Menu::Top::ID_ALERTS: {
                // Toggle alerts mute state
                bool newState = !menu.getAltIcon(id);
                presenter->saveMuteAlerts(newState);
                mAlertsMuted = newState;
                menu.setAltIcon(id, newState);
            } break;
            case Gui::Config::Menu::Top::ID_SLEEP: {
                // Shown at once; the backend owns the state and answers with
                // it, which also brings the Alerts entry up to date.
                const bool newState = !mSleepActive;
                presenter->setSleep(newState);
                setSleep(newState);
            } break;
//            case Gui::Config::Menu::Top::ID_FIND_PHONE:
//                application().gotoTopMenuFindScreenNoTransition();
//                break;

            default:
                if (appIdx >= 0 && mAppList != nullptr
                        && mAppList->size() > static_cast<size_t>(appIdx)) {
                    presenter->appStart((*mAppList)[appIdx].pidGUI);
                }
                break;
        };
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoHomeScreenNoTransition();
    }
}


