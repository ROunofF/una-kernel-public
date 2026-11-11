#include <gui/mediacontrol_screen/MediaControlView.hpp>
#include <gui/common/GuiConfig.hpp>
#include <touchgfx/Unicode.hpp>

namespace {
// Progress line geometry (mirrors the generated background line: start x = 4,
// end x = 186, drawn at y = 4 within the 190 px wide widget).
constexpr int kProgLineY = 4;
constexpr int kProgStartX = 4;
constexpr int kProgSpanX = 182;  // 186 - 4

// Ticks in one second, used to advance elapsed locally while playing.
constexpr uint32_t kTicksPerSecond = GUI_CONFIG_MS_2_TICKS(1000);
}

MediaControlView::MediaControlView()
    : mTracker(kTicksPerSecond)
{

}

void MediaControlView::setupScreen()
{
    MediaControlViewBase::setupScreen();

    // The title starts scrolling only after 0.5s. The widget counts in ticks, so
    // the tick-rate conversion lives here, not in the widget.
    title.setScrollPauseTicks(GUI_CONFIG_MS_2_TICKS(500));

    // Indicate the active controls: prev/next (top), play-pause (bottom-left),
    // exit (bottom-right).
    buttons.setL1(Buttons::WHITE);
    buttons.setR1(Buttons::WHITE);
    buttons.setL2(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);

    render();   // initial play/pause glyph + empty progress
}

void MediaControlView::tearDownScreen()
{
    MediaControlViewBase::tearDownScreen();
}

void MediaControlView::handleKeyEvent(uint8_t key)
{
    switch (key) {
        case Gui::Config::Button::L1:
            mTracker.onSkip();
            presenter->prevTrack();
            render();
            break;
        case Gui::Config::Button::R1:
            mTracker.onSkip();
            presenter->nextTrack();
            render();
            break;
        case Gui::Config::Button::L2:
            // Send the command for the target state; the tracker flips the belief
            // optimistically so the bar/glyph react at once.
            if (mTracker.onTogglePause()) {
                presenter->pause();
            } else {
                presenter->play();
            }
            render();
            break;
        case Gui::Config::Button::R2:
            presenter->exit();
            break;
        default:
            break;
    }
}

void MediaControlView::handleTickEvent()
{
    // The tracker advances the position while playing and reports when it moved.
    if (mTracker.onTick()) {
        render();
    }
}

void MediaControlView::setNowPlaying(const std::shared_ptr<NowPlaying>& np)
{
    mTracker.onSnapshot(np.get());

    // Top line: track title, or the player name when the track has no title (the
    // artist has its own line below). The scrolling widget centers it or marquees
    // it if it does not fit.
    const std::string& track = !mTracker.trackTitle().empty()
            ? mTracker.trackTitle() : mTracker.playerName();
    title.setText(track.c_str());

    // Bottom line: artist (center-aligned, auto-ellipsis in the base; no scroll).
    Unicode::fromUTF8(
            reinterpret_cast<const uint8_t*>(mTracker.trackArtist().c_str()),
            artistBuffer, ARTIST_SIZE);
    // fromUTF8 does not append a terminator when the source fills the buffer, so
    // a too-long artist would render past the array as garbage. Force a terminator.
    artistBuffer[ARTIST_SIZE - 1] = 0;
    artist.invalidate();

    render();
}

void MediaControlView::render()
{
    // Play/pause glyph reflects the action the button performs: the play
    // triangle when paused (press to play), the pause bars when playing.
    const bool paused = mTracker.paused();
    playIcon.setVisible(paused);
    playIcon.invalidate();
    pauseIcon.setVisible(!paused);
    pauseIcon.invalidate();

    const bool hasTime = mTracker.hasTime();

    float fraction = 0.0f;
    if (hasTime) {
        fraction = mTracker.elapsedSec() / mTracker.durationSec();
        if (fraction < 0.0f) {
            fraction = 0.0f;
        }
        if (fraction > 1.0f) {
            fraction = 1.0f;
        }
    }
    progress.setEnd(kProgStartX + static_cast<int>(fraction * kProgSpanX), kProgLineY);
    // Repaint the full-width background line as well: when the bar shrinks (e.g. a
    // new track resets elapsed) invalidating only the shorter active line leaves
    // the old, longer segment on screen. The background strip clears those pixels.
    background.invalidate();
    progress.invalidate();

    time.setVisible(hasTime);
    if (hasTime) {
        int remaining = static_cast<int>(mTracker.durationSec() - mTracker.elapsedSec());
        if (remaining < 0) {
            remaining = 0;
        }
        Unicode::snprintf(timeBuffer, TIME_SIZE, "%d:%02d", remaining / 60, remaining % 60);
    }
    time.invalidate();
}
