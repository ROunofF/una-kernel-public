#ifndef USERAPPVIEW_HPP
#define USERAPPVIEW_HPP

#include <gui_generated/userapp_screen/UserAppViewBase.hpp>
#include <gui/userapp_screen/UserAppPresenter.hpp>
#include <gui/containers/CountdownTimer.hpp>

class UserAppView : public UserAppViewBase
{
public:
    UserAppView();
    virtual ~UserAppView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /**
     * @brief Say which app this screen is showing.
     *
     *        Both ways in come through here: entering the screen, and one app
     *        replacing another without leaving it.
     */
    void setApp(uint32_t pid);

    /**
     * @brief Point the screen at the surface apps draw into.
     * @param info Surface as announced by the backend.
     */
    void setAppSurface(const AppSurfaceInfo &info);

protected:
    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

    uint32_t mPid = 0;

    AppSurfaceInfo mSurfaceInfo {};

    /// Take down whatever is on screen and start waiting for the app to draw.
    void awaitFirstFrame();

    /// Show the app's frame, if the surface currently holds one of its own.
    void refreshFromSurface();

    // Raises the label if the app takes too long over its first frame
    void onFirstFrameOverdue();
    CountdownTimer                       mFirstFrameTimer;
    touchgfx::Callback<UserAppView>      mFirstFrameCb;
};

#endif // USERAPPVIEW_HPP
