#ifndef HOMEVIEW_HPP
#define HOMEVIEW_HPP

#include <gui_generated/home_screen/HomeViewBase.hpp>
#include <gui/home_screen/HomePresenter.hpp>

class HomeView : public HomeViewBase
{
public:
    HomeView();
    virtual ~HomeView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setDailyHealth(uint32_t steps, uint32_t stepsGoal, 
        uint32_t floors, uint32_t floorsGoal, 
        uint32_t activity, uint32_t activityGoal);

    void setHomeWidget(const HomeWidgetData &widget);

    void setMuteAlerts(bool muted);
    void setTime(uint8_t h, uint8_t m, uint8_t s);
    void setDate(uint8_t m, uint8_t d, uint8_t wd);
    void setClockSettings(const WatchSettingsLocal::Clock &clock);
    void setBatteryLevel(uint8_t level);

    /**
     * @brief Say which app the watch wears as its face, 0 for the built-in one.
     *
     *        Naming an app takes the built-in face down at once, rather than
     *        letting it stand in: the two are different clocks, and either
     *        wearing the other's place reads as the watch changing its mind.
     *        The background is what shows until the named app has drawn.
     */
    void setWatchFace(uint32_t guiPid);

    /**
     * @brief Point the screen at the surface apps draw into.
     * @param info Surface as announced by the backend.
     */
    void setAppSurface(const AppSurfaceInfo &info);

protected:
    void handleKeyEvent(uint8_t key) override;
    void handleTickEvent() override;

    /// Show the face's frame, once the surface holds one drawn for this screen.
    void refreshFromSurface();

    /// GUI process of the face being shown, 0 while the built-in one is up.
    uint32_t mPid = 0;

    AppSurfaceInfo mSurfaceInfo {};
};

#endif // HOMEVIEW_HPP
