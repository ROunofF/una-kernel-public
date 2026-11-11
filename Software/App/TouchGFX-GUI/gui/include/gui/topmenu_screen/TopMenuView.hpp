#ifndef TOPMENUVIEW_HPP
#define TOPMENUVIEW_HPP

#include <gui_generated/topmenu_screen/TopMenuViewBase.hpp>
#include <gui/topmenu_screen/TopMenuPresenter.hpp>
#include <gui/topmenu_screen/TopMenuOrder.hpp>
#include <array>

class TopMenuView : public TopMenuViewBase
{
public:
    TopMenuView();
    virtual ~TopMenuView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();
    void setAlertMute(bool muted);
    void setSleep(bool active);

    void updAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>> &list);
    void updAppIcons(App::Type type, const App::IconRef *icons, uint8_t count);

    void clearIcons();

protected:
    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

private:
    /// Utility apps the menu can hold alongside the fixed entries. The same cap
    /// the order is built with, so mUserAppNames is indexed by exactly the
    /// range buildOrder() can produce.
    static constexpr int16_t kMaxUserApps = Gui::Config::Menu::Top::kMaxOrderApps;

    /// Converted app names. The menu holds pointers into these, so they must
    /// live as long as the menu is configured; the icons stay in mAppList and
    /// the menu owns the bitmaps it makes from them.
    struct UserAppName {
        Unicode::UnicodeChar name[App::kNameLen];
    };
    std::array<UserAppName, kMaxUserApps> mUserAppNames {};

    std::shared_ptr<std::vector<App::Item>> mAppList {};

    /// What each menu item refers to, rebuilt whenever the app list changes.
    /// Fixed entries and apps interleave -- Alarm sits above Alerts, Stopwatch
    /// and Timer below Settings -- so an item id no longer says which it is.
    std::array<Gui::Config::Menu::Top::ItemRef, MenuTop::kMaxItems> mItems {};
    int16_t mItemCount = 0;

    /// Icons for the apps currently on the arc, as last handed over. Counted
    /// references, so the pixels a widget is drawing outlive the loader letting
    /// go of them; the menu points straight into these buffers.
    App::IconRef mIcons[App::kIconWindowMax];
    uint8_t      mIconCount = 0;

    // ---- Holding a button to run through the list -------------------------
    //
    // Stepping is driven by the press rather than the click, so the arc moves
    // as the button goes down instead of when it comes up, and one press is one
    // step however long it is held. Keeping the button down then runs on: after
    // kHoldDelay, a step every kHoldPeriod, quickening to kHoldPeriodFast once
    // it is clear the user means to travel. A hundred entries is a long way at
    // one press each.

    /// Held before the run starts, so a deliberate single step does not run on.
    static constexpr uint32_t kHoldDelay      = GUI_CONFIG_MS_2_TICKS(500);
    /// Ticks between steps once running, and once running quickly.
    static constexpr uint32_t kHoldPeriod     = 2;
    static constexpr uint32_t kHoldPeriodFast = 1;
    /// Spent running before it quickens.
    static constexpr uint32_t kHoldAccelAfter = GUI_CONFIG_MS_2_TICKS(1500);

    /// The button being held, 0 for none. Its release ends the run, whatever
    /// else that press turned into on the way.
    uint8_t  mHeldKey   = 0;
    /// Ticks since the press, which is what the delays above are measured on.
    uint32_t mHeldTicks = 0;

    /// Step the selection, and say whether it moved. The ends of the list are
    /// ends: holding against one of them stops rather than wraps.
    bool step(bool forward);

    /// Tell the loader which apps' icons the arc needs now, and drop the rest.
    void requestIcons();
    /// Point the menu at whichever of the arc's icons have arrived.
    void applyIcons();
    /// The item index showing @p appId, or -1.
    int16_t itemForAppId(uint64_t appId) const;

    /**
     * @brief What the selection was, in terms that outlive the order changing.
     *
     * Item ids move as apps come and go, so the cursor cannot be remembered as
     * one. An app is remembered by its own id and a fixed entry by which one it
     * is; @c rawId is the one case where only a position is available -- a
     * position the presenter restored, which was an index into the menu as it
     * stood when the screen was last open, and that menu is gone.
     */
    struct Selection {
        uint64_t appId = 0;     ///< The app that was selected, 0 for none.
        int16_t  fixed = -1;    ///< The fixed entry that was selected, -1 for none.
        int16_t  rawId = -1;    ///< A restored position, -1 for none.
    };

    /// What is selected now. Read before mAppList is replaced: it resolves an
    /// app through the list the current table was built from.
    Selection captureSelection() const;

    /// Rebuild mItems from mAppList and put every item on the menu. Returns the
    /// item @p keep now lives at.
    int16_t rebuildItems(const Selection &keep);
    /// Give one menu item its fixed entry's name and icons.
    void configFixed(int16_t id, int16_t fixedId);
    /// The app list index item @p id shows, or -1 for a fixed entry.
    int16_t appIndexForItem(int16_t id) const;

    /// Position the presenter restored, kept until the app list arrives and the
    /// menu is finally long enough to hold it. -1 once the user scrolls.
    int16_t mRequestedId = -1;

    /// Whether alerts are muted, as last told. Held because the Alerts entry is
    /// reconfigured, and can move, every time the order is rebuilt -- and the
    /// presenter only says so once, before the app list has even arrived.
    bool mAlertsMuted = false;

    /// Whether sleep mode is on, as last told. Held for the same reason.
    bool mSleepActive = false;

    /// Show a fixed toggle entry in its alternate state, wherever it now is.
    void setFixedAlt(int16_t fixedId, bool alt);
};

#endif // TOPMENUVIEW_HPP
