#ifndef TRACKINGMENUVIEW_HPP
#define TRACKINGMENUVIEW_HPP

#include <gui_generated/trackingmenu_screen/TrackingMenuViewBase.hpp>
#include <gui/trackingmenu_screen/TrackingMenuPresenter.hpp>
#include <gui/containers/CountdownTimer.hpp>

class TrackingMenuView : public TrackingMenuViewBase
{
public:
    TrackingMenuView();
    virtual ~TrackingMenuView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();
    void setAlertMute(bool muted);

    void updAppList(App::Type type, const std::shared_ptr<std::vector<App::Item>>& list);
    void updAppIcons(App::Type type, const App::IconRef *icons, uint8_t count);
protected:
    std::shared_ptr<std::vector<App::Item>> mAppList {};

    /// Icons for the rows, as last handed over. Counted references, so the
    /// pixels a row is drawing outlive the loader letting go of them; OrbitMenu
    /// points straight into these buffers.
    App::IconRef mIcons[App::kIconWindowMax];
    uint8_t      mIconCount = 0;

    /// Tell the loader which apps' icons the wheel needs now, and drop the rest.
    void requestIcons();
    /// Point the wheel's rows at whichever of its icons have arrived.
    void applyIcons();
    /// The set for @p appId, or nullptr if it has not arrived.
    const App::IconSet *iconFor(uint64_t appId) const;
    /// The row index showing @p appId, or -1.
    int16_t indexForAppId(uint64_t appId) const;

    /**
     * @brief Where the wheel was, in terms that outlive the list re-sorting.
     *
     * A row index does not survive a refreshed list: the backend orders
     * activities most-used first, and launching one moves it to the front, so
     * the row the user was on means a different app by the time the list comes
     * back. @c rawRow is the one case where only a position is available -- a
     * position the presenter restored, which indexed a list that is gone.
     */
    struct Selection {
        uint64_t appId  = 0;    ///< The app that was selected, 0 for none.
        int16_t  rawRow = -1;   ///< A restored position, -1 for none.
    };

    /// Where the wheel is now. Read before mAppList is replaced: it resolves
    /// the row through the list that row was an index into.
    Selection captureSelection() const;

    void updateMenu() { updateMenu(captureSelection()); }
    void updateMenu(const Selection &keep);
    virtual void handleKeyEvent(uint8_t key) override;

    void onDismiss();
    CountdownTimer                        mDismissTimer;
    touchgfx::Callback<TrackingMenuView>  mDismissCb;

    char mMsgBuf[24] {}; ///< UTF-8 scratch for the "no apps" placeholder label.

    /// Position the presenter restored, kept until the app list arrives and the
    /// wheel finally has the rows to hold it. -1 once the user scrolls.
    int16_t mRequestedId = -1;
};

#endif // TRACKINGMENUVIEW_HPP
