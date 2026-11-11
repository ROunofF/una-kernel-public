#ifndef SETTINGSWATCHFACEVIEW_HPP
#define SETTINGSWATCHFACEVIEW_HPP

#include <gui_generated/settingswatchface_screen/SettingsWatchFaceViewBase.hpp>
#include <gui/settingswatchface_screen/SettingsWatchFacePresenter.hpp>

/**
 * @brief   Picks a watch face by showing it.
 * @note    Each candidate is run for real and shown here, so what the user
 *          judges is the face itself. The built-in one is the first position
 *          and is drawn by this screen, the rest arrive on the frame surface.
 */
class SettingsWatchFaceView : public SettingsWatchFaceViewBase
{
public:
    SettingsWatchFaceView();
    virtual ~SettingsWatchFaceView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setDailyHealth(uint32_t steps, uint32_t stepsGoal,
        uint32_t floors, uint32_t floorsGoal,
        uint32_t activity, uint32_t activityGoal);

    void setMuteAlerts(bool muted);
    void setTime(uint8_t h, uint8_t m, uint8_t s);
    void setDate(uint8_t m, uint8_t d, uint8_t wd);
    void setClockSettings(const WatchSettingsLocal::Clock &clock);
    void setBatteryLevel(uint8_t level);

    /**
     * @brief Say which app is drawing the face being previewed, 0 for the
     *        built-in one.
     *
     *        Naming an app takes this screen's own face down, but never a face
     *        already on the surface: stepping between two of them holds the one
     *        that is up until the next has drawn, so only the step off the
     *        built-in position shows the background in between.
     */
    void setWatchFace(uint32_t guiPid);

    /**
     * @brief Say how many faces there are to step through and which one is up.
     * @note  A count of zero leaves the position blank, which is what to show
     *        while the list is still on its way.
     */
    void setCursor(uint8_t count, uint8_t index);

    /**
     * @brief Move one place through the list.
     * @param delta -1 for the previous face, +1 for the next.
     */
    void stepCursor(int8_t delta);

    /**
     * @brief Point the screen at the surface apps draw into.
     * @param info Surface as announced by the backend.
     */
    void setAppSurface(const AppSurfaceInfo &info);

protected:
    void handleKeyEvent(uint8_t key) override;
    void handleTickEvent() override;

    /// Show the previewed face's frame, if the surface holds one of its own.
    void refreshFromSurface();

    /// GUI process of the face being previewed, 0 while the built-in one is up.
    uint32_t mPid = 0;

    AppSurfaceInfo mSurfaceInfo {};
};

#endif // SETTINGSWATCHFACEVIEW_HPP
