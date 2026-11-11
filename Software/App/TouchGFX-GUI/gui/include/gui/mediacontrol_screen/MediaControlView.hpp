#ifndef MEDIACONTROLVIEW_HPP
#define MEDIACONTROLVIEW_HPP

#include <memory>

#include <gui_generated/mediacontrol_screen/MediaControlViewBase.hpp>
#include <gui/mediacontrol_screen/MediaControlPresenter.hpp>

// NowPlaying and NowPlayingTracker arrive transitively via the presenter ->
// ModelListener -> Model -> GuiBackendInterface include chain, which is the one
// place the simulator/firmware include paths for shared types are resolved.

class MediaControlView : public MediaControlViewBase
{
public:
    MediaControlView();
    virtual ~MediaControlView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

    /** Apply a Now Playing snapshot (title, remaining time, progress). */
    void setNowPlaying(const std::shared_ptr<NowPlaying>& np);

protected:
    // Interprets snapshots, key presses and ticks into the pause belief, display
    // title and extrapolated position this view draws. Host-tested; keeps the one
    // stateful, timing-sensitive piece out of the view (see NowPlayingTracker).
    NowPlayingTracker mTracker;

    void render();
};

#endif // MEDIACONTROLVIEW_HPP
