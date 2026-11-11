#include <gui/mediacontrol_screen/MediaControlView.hpp>
#include <gui/mediacontrol_screen/MediaControlPresenter.hpp>

MediaControlPresenter::MediaControlPresenter(MediaControlView& v)
    : view(v)
{

}

void MediaControlPresenter::activate()
{
    // Arm the idle timeout for this screen (it opens on a button hold, not a key
    // event that would otherwise re-arm it).
    model->resetIdleTimer();

    // Seed the screen with the current snapshot; later changes arrive via
    // onMediaNowPlaying().
    view.setNowPlaying(model->mediaGetNowPlaying());
}

void MediaControlPresenter::deactivate()
{

}

void MediaControlPresenter::onMediaNowPlaying(std::shared_ptr<NowPlaying> nowPlaying)
{
    view.setNowPlaying(nowPlaying);
}

void MediaControlPresenter::prevTrack()
{
    model->mediaRemoteCommand(MediaCommand::PreviousTrack);
}

void MediaControlPresenter::nextTrack()
{
    model->mediaRemoteCommand(MediaCommand::NextTrack);
}

void MediaControlPresenter::play()
{
    model->mediaRemoteCommand(MediaCommand::Play);
}

void MediaControlPresenter::pause()
{
    model->mediaRemoteCommand(MediaCommand::Pause);
}

void MediaControlPresenter::exit()
{
    model->switchToNextPriorityScreen();
}
