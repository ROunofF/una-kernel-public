#include <gui/trackingmenu_screen/TrackingMenuView.hpp>
#include <gui/common/GuiConfig.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>

static constexpr uint16_t kDismissTicks = GUI_CONFIG_MS_2_TICKS(2000);
// OrbitMenu copies at most this many, so there is no point carrying more.
static const int16_t      kMaxItems     = OrbitMenu::kMaxEntries;

TrackingMenuView::TrackingMenuView()
    : mDismissCb(this, &TrackingMenuView::onDismiss)
{
}

void TrackingMenuView::setupScreen()
{
    TrackingMenuViewBase::setupScreen();

    title.set(T_TEXT_TRACK_UC);

    // Physical-button hints: L1/L2 scroll (shown by the scroll indicator),
    // R1 starts the app, R2 goes back. R1 colour is set per list state.
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);

    mDismissTimer.setDuration(kDismissTicks);
    mDismissTimer.setCallback(mDismissCb);
    mDismissTimer.start();

    // Fixed 3-row layout
    orbitMenu.setXY(0, 47);
    orbitMenu.setHeight(150);
    orbitMenu.setCenterOffset(0);
    const OrbitMenu::Anchors anchors = { {  0, 22,  87 },  // centre (60px icon)
                                         { 50, 52,  95 },  // +/-1   (30px icon, pushed right)
                                         { 90, 71, 108 } };// +/-2
    orbitMenu.setAnchors(anchors);
    orbitMenu.setCircularMinItems(3);
    orbitMenu.setAnimationSteps(4);

    updateMenu();
}

void TrackingMenuView::tearDownScreen()
{
    TrackingMenuViewBase::tearDownScreen();
}

void TrackingMenuView::setPositionId(uint16_t id)
{
    // The wheel has no rows yet at this point, so the position only sticks once
    // updateMenu() has the app list; it re-applies it from here.
    mRequestedId = static_cast<int16_t>(id);
    orbitMenu.setSelected(mRequestedId);
    scrollIndicator.setActiveId(id);
}

uint16_t TrackingMenuView::getPositionId()
{
    // A request is only outstanding while the wheel is too short to hold it, so
    // its selection is a clamped version - report what was asked for instead, or
    // leaving the screen would persist the clamped position.
    if (mRequestedId >= 0) {
        return static_cast<uint16_t>(mRequestedId);
    }
    return orbitMenu.getSelected();
}

void TrackingMenuView::setAlertMute(bool muted)
{
    if (muted) {
        touchgfx::Unicode::snprintf(infoBuffer, INFO_SIZE, "%s",
                                    touchgfx::TypedText(T_TEXT_ALERTS_MUTED).getText());
        info.setColor(Gui::Config::Color::YELLOW_DARK);
        info.setVisible(true);
    } else {
        info.setVisible(false);
    }
    info.invalidate();
}

void TrackingMenuView::updAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>>& list)
{
    (void)type;

    // Read before the list is replaced: the row is only meaningful against the
    // list it indexed, and the incoming one is re-sorted whenever a launch has
    // changed the most-used order.
    const Selection keep = captureSelection();

    mAppList = list;
    updateMenu(keep);
    requestIcons();
}

TrackingMenuView::Selection TrackingMenuView::captureSelection() const
{
    Selection keep;

    if (mRequestedId >= 0) {
        // A position the presenter restored. Only a position: it indexed the
        // list as it stood when the screen was last open.
        keep.rawRow = mRequestedId;
        return keep;
    }

    const int16_t row = orbitMenu.getSelected();
    if (mAppList != nullptr && row >= 0
            && static_cast<size_t>(row) < mAppList->size()) {
        keep.appId = (*mAppList)[row].appId;
    }

    return keep;
}

void TrackingMenuView::updAppIcons(App::Type type, const App::IconRef *icons, uint8_t count)
{
    (void)type;

    if (count > App::kIconWindowMax) {
        count = App::kIconWindowMax;
    }

    // Re-pointed entry by entry rather than by rebuilding the list: setItems()
    // rewinds the wheel, which would drag the selection back to the top every
    // time a window of icons landed. Both loops are bounded by the window, so
    // this costs the same however long the list gets.
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

        const int16_t index = indexForAppId(goingId);
        if (index >= 0) {
            orbitMenu.setEntryIcons(index, nullptr, nullptr);
        }
    }

    // Only now: the loop above had to unpoint the rows from these buffers
    // before the last reference to them went.
    for (uint8_t i = 0; i < App::kIconWindowMax; ++i) {
        mIcons[i] = (i < count) ? icons[i] : App::IconRef();
    }
    mIconCount = count;

    applyIcons();
}

int16_t TrackingMenuView::indexForAppId(uint64_t appId) const
{
    if (mAppList == nullptr) {
        return -1;
    }

    int16_t count = static_cast<int16_t>(mAppList->size());
    if (count > kMaxItems) {
        count = kMaxItems;
    }

    for (int16_t i = 0; i < count; i++) {
        if ((*mAppList)[i].appId == appId) {
            return i;
        }
    }

    return -1;
}

void TrackingMenuView::applyIcons()
{
    if (mAppList == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < mIconCount; ++i) {
        if (!mIcons[i]) {
            continue;
        }

        const int16_t index = indexForAppId(mIcons[i]->appId);
        if (index >= 0) {
            orbitMenu.setEntryIcons(index,
                    mIcons[i]->icon.data(), mIcons[i]->smallIcon.data());
        }
    }
}

const App::IconSet *TrackingMenuView::iconFor(uint64_t appId) const
{
    for (uint8_t i = 0; i < mIconCount; ++i) {
        if (mIcons[i] && mIcons[i]->appId == appId) {
            return mIcons[i].get();
        }
    }

    return nullptr;
}

void TrackingMenuView::requestIcons()
{
    if (mAppList == nullptr) {
        presenter->requestIconWindow(nullptr, 0);
        return;
    }

    // Only the rows the wheel is showing, or about to. Asking for the whole
    // list was affordable while it held sixteen and stops being so as it grows;
    // this way what the icons cost is the window, not the list.
    int16_t indices[G2BEvent::UserAppIconWindow::kMax];
    const int16_t found = orbitMenu.getIconWindow(indices,
            static_cast<int16_t>(G2BEvent::UserAppIconWindow::kMax));

    int16_t count = static_cast<int16_t>(mAppList->size());
    if (count > kMaxItems) {
        count = kMaxItems;
    }

    uint64_t appIds[G2BEvent::UserAppIconWindow::kMax];
    uint8_t wanted = 0;

    for (int16_t i = 0; i < found; i++) {
        if (indices[i] < 0 || indices[i] >= count) {
            continue;
        }
        appIds[wanted++] = (*mAppList)[indices[i]].appId;
    }

    presenter->requestIconWindow(appIds, wanted);
}

void TrackingMenuView::updateMenu(const Selection &keep)
{
    const bool hasApps = (mAppList != nullptr && !mAppList->empty());

    if (!hasApps) {
        buttons.setR1(Buttons::NONE);
        scrollIndicator.setCount(0);

        if (mDismissTimer.isRunning()) {
            // Still settling right after opening -- show nothing yet.
            orbitMenu.setItems(nullptr, 0);
        } else {
            // Timed out with no apps -- show a single placeholder row.
            touchgfx::Unicode::toUTF8(touchgfx::TypedText(T_TEXT_NO_APPS).getText(),
                                      reinterpret_cast<uint8_t *>(mMsgBuf), sizeof(mMsgBuf));
            OrbitMenu::Entry placeholder{};
            placeholder.icon60 = touchgfx::BITMAP_INVALID; // text-only row
            placeholder.icon30 = touchgfx::BITMAP_INVALID;
            placeholder.label  = mMsgBuf;
            orbitMenu.setItems(&placeholder, 1);
        }
        return;
    }

    int16_t count = static_cast<int16_t>(mAppList->size());
    if (count > kMaxItems) {
        count = kMaxItems;
    }

    // Applied below; a later refresh must go with the live selection rather than
    // drag the cursor back here.
    mRequestedId = -1;

    OrbitMenu::Entry entries[kMaxItems] {};
    for (int16_t i = 0; i < count; i++) {
        const App::Item &item = (*mAppList)[i];
        entries[i].label = item.name;             // app name (UTF-8)

        // Null until the icons arrive, which is a row drawn as label-only for
        // the tick or two in between.
        if (const App::IconSet *set = iconFor(item.appId)) {
            entries[i].ext60 = set->icon.data();      // 60x60 ABGR2222
            entries[i].ext30 = set->smallIcon.data(); // 30x30 ABGR2222
        }
    }
    orbitMenu.setItems(entries, count);

    // setItems() rewinds to the first row, so put the selection back where
    // whatever was selected has ended up. By app id where there is one, because
    // the list arriving here is re-sorted after every launch and the row the
    // user was on now holds a different app -- restoring the row landed the
    // cursor one place off, on the app that used to sit above the one just
    // used.
    int16_t restored = -1;

    if (keep.appId != 0) {
        for (int16_t i = 0; i < count; ++i) {
            if ((*mAppList)[i].appId == keep.appId) {
                restored = i;
                break;
            }
        }
    }

    if (restored < 0 && keep.rawRow >= 0) {
        // A restored position, applied as one. Fewer apps than last time: hold
        // the end of the list rather than rewind.
        restored = (keep.rawRow < count) ? keep.rawRow : static_cast<int16_t>(count - 1);
    }

    if (restored < 0) {
        restored = 0;   // the app that was selected is gone
    }

    orbitMenu.setSelected(restored);

    scrollIndicator.setCount(static_cast<uint16_t>(count));
    scrollIndicator.setActiveId(static_cast<uint16_t>(restored));
    buttons.setR1(Buttons::AMBER);
}

void TrackingMenuView::handleKeyEvent(uint8_t key)
{
    const int16_t count = (mAppList != nullptr) ? static_cast<int16_t>(mAppList->size()) : 0;

    if (key == Gui::Config::Button::L1) {
        if (count > 1) {
            mRequestedId = -1; // the user is driving now
            const int16_t before = orbitMenu.getSelected();
            orbitMenu.selectPrev();
            if (orbitMenu.getSelected() != before) {
                // Step by -1 (wraps): correct direction in both bounded and circular modes.
                scrollIndicator.animateToId(static_cast<int16_t>(scrollIndicator.getActiveId()) - 1,
                                            Gui::Config::kMenuAnimationSteps);
                requestIcons();  // the wheel has turned: different rows are showing
            }
        }
    } else if (key == Gui::Config::Button::L2) {
        if (count > 1) {
            mRequestedId = -1;
            const int16_t before = orbitMenu.getSelected();
            orbitMenu.selectNext();
            if (orbitMenu.getSelected() != before) {
                scrollIndicator.animateToId(static_cast<int16_t>(scrollIndicator.getActiveId()) + 1,
                                            Gui::Config::kMenuAnimationSteps);
                requestIcons();
            }
        }
    } else if (key == Gui::Config::Button::R1) {
        if (count > 0) {
            mRequestedId = -1; // the user acted on what is on screen
            uint32_t id  = orbitMenu.getSelected();
            uint32_t pid = (*mAppList)[id].pidGUI;
            presenter->appStart(pid);
        }
    } else if (key == Gui::Config::Button::R2) {
        application().gotoHomeScreenNoTransition();
    }
}

void TrackingMenuView::onDismiss()
{
    if (mAppList == nullptr || mAppList->empty()) {
        updateMenu(); // timer elapsed -> replace blank with the "no apps" placeholder
    }
}
