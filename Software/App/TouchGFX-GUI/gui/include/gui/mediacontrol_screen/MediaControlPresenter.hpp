#ifndef MEDIACONTROLPRESENTER_HPP
#define MEDIACONTROLPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MediaControlView;

class MediaControlPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MediaControlPresenter(MediaControlView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~MediaControlPresenter() {}

    /** Latest Now Playing snapshot pushed from the model. */
    virtual void onMediaNowPlaying(std::shared_ptr<NowPlaying> nowPlaying) override;

    /** Idle timeout: leave the screen, resuming a preempted app or going Home. */
    virtual void onIdleTimeout() override { model->switchToNextPriorityScreen(); }

    // Button actions, called by the view.
    void prevTrack();
    void nextTrack();
    void play();
    void pause();
    void exit();

private:
    MediaControlPresenter();

    MediaControlView& view;
};

#endif // MEDIACONTROLPRESENTER_HPP
