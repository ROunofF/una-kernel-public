/**
 ******************************************************************************
 * @file    NowPlayingTracker.hpp
 * @brief   Interprets Now Playing snapshots into a ready-to-render media state.
 ******************************************************************************
 *
 * MediaService produces the authoritative NowPlaying snapshot and two freshness
 * tokens (elapsedSeq / playbackSeq), but leaves them raw. This tracker turns a
 * stream of snapshots plus a periodic tick into the state a view actually draws:
 * a display title, a pause belief, and a locally extrapolated playback position.
 *
 * It keeps the one stateful, timing-sensitive piece of the media screen out of
 * the (untested) view: a view holds a tracker, feeds it snapshots / key presses
 * / ticks, and renders only its getters. The tracker carries no TouchGFX, BLE or
 * OS types (only NowPlaying), so it is host-testable and any second Now Playing
 * consumer can reuse it instead of re-deriving the token bookkeeping.
 *
 ******************************************************************************
 */

#ifndef NOW_PLAYING_TRACKER_HPP
#define NOW_PLAYING_TRACKER_HPP

#include <cstdint>
#include <string>

// Sibling header - resolves in every build without the SIMULATOR path split.
#include "NowPlaying.hpp"

/**
 * @brief   Interprets Now Playing snapshots into a ready-to-render media state.
 */
class NowPlayingTracker {
public:

    /**
     * @brief   Construct the tracker.
     * @param   ticksPerSecond: view ticks in one second, used to advance the
     *          extrapolated position once a second while playing. Injected so the
     *          tracker stays free of view/GUI config and is host-testable.
     */
    explicit NowPlayingTracker(uint32_t ticksPerSecond)
        : mTicksPerSecond(ticksPerSecond)
    {
    }

    /**
     * @brief   Apply a Now Playing snapshot (nullptr clears the position).
     */
    void onSnapshot(const NowPlaying* np)
    {
        if (np == nullptr) {
            mTrackTitle.clear();
            mTrackArtist.clear();
            mElapsedKnown = false;
            mDurationSec = 0.0f;
            return;
        }

        // Raw track fields for the two-line view (track title / artist). The top
        // line falls back to the player name (below) when there is no track title.
        mTrackTitle = np->title;
        mTrackArtist = np->artist;

        // A different player took over: forget the previous player's pause
        // learning and belief (a fresh report below re-establishes them).
        if (np->playerName != mLastPlayer) {
            mLastPlayer = np->playerName;
            mPaused = false;
            mPlayerReportsPause = false;
        }

        mRate = (np->rate && *np->rate > 0.0f) ? *np->rate : 1.0f;
        mDurationSec = np->durationSec ? *np->durationSec : 0.0f;

        // Re-correct the pause belief from a fresh PlaybackInfo (state is always
        // present in one, even without elapsed - e.g. "0,," on pause). A "paused"
        // report is always trusted (and teaches us this player reports pause); a
        // "playing" report is trusted only from such a player, so a player that
        // always says "playing" (VLC) cannot undo a watch-set pause. Gated on
        // playbackSeq so re-emits carrying the last state (a volume change) are
        // ignored.
        if (np->playbackSeq != mLastPlaybackSeq) {
            mLastPlaybackSeq = np->playbackSeq;
            if (np->state == NowPlaying::State::Paused) {
                mPaused = true;
                mPlayerReportsPause = true;
            } else if (np->state == NowPlaying::State::Playing &&
                    mPlayerReportsPause) {
                mPaused = false;
            }
        }

        // Adopt only a freshly reported elapsed (seq changed); a snapshot that
        // merely carries the last elapsed must not reset the local extrapolation.
        if (np->elapsedSec) {
            if (np->elapsedSeq != mLastElapsedSeq) {
                mElapsedSec = *np->elapsedSec;
                mLastElapsedSeq = np->elapsedSeq;
                mTickAccum = 0;
            }
            mElapsedKnown = true;
        } else {
            // No position (stream / player that does not report it) - hide the
            // bar/time, but keep the pause belief (driven by state, not elapsed).
            mElapsedKnown = false;
        }
    }

    /**
     * @brief   A track skip (prev/next) from the watch: playback resumes, so
     *          clear the pause belief (a state-reporting player re-confirms it).
     */
    void onSkip()
    {
        mPaused = false;
    }

    /**
     * @brief   A play/pause toggle from the watch.
     * @retval  The new belief (true = now paused). Flipped optimistically so the
     *          bar/glyph react at once even for players that never report pause;
     *          a reported state re-corrects it. The caller sends the matching
     *          command for the returned state.
     */
    bool onTogglePause()
    {
        mPaused = !mPaused;
        return mPaused;
    }

    /**
     * @brief   Advance one view tick.
     * @retval  'true' if the extrapolated position moved (the caller re-renders).
     * @note    While playing, the position steps by 'rate' once a second; paused
     *          or position-less state freezes it.
     */
    bool onTick()
    {
        if (mPaused) {
            return false;
        }
        if (!mElapsedKnown || mDurationSec <= 0.0f) {
            return false;
        }
        if (++mTickAccum < mTicksPerSecond) {
            return false;
        }
        mTickAccum = 0;
        mElapsedSec += mRate;
        if (mElapsedSec > mDurationSec) {
            mElapsedSec = mDurationSec;
        }
        return true;
    }

    // --- Render inputs --------------------------------------------------------

    /**
     * @brief   The raw track title (empty if the player did not report one).
     */
    const std::string& trackTitle() const { return mTrackTitle; }

    /**
     * @brief   The raw track artist (empty if the player did not report one).
     */
    const std::string& trackArtist() const { return mTrackArtist; }

    /**
     * @brief   The active player's name (empty if not reported). Used as the
     *          top-line fallback when there is no track title.
     */
    const std::string& playerName() const { return mLastPlayer; }

    /**
     * @brief   Whether playback is believed paused (glyph + frozen bar).
     */
    bool paused() const { return mPaused; }

    /**
     * @brief   Whether a playback position is known (else hide bar and time).
     */
    bool hasTime() const { return mElapsedKnown && mDurationSec > 0.0f; }

    /**
     * @brief   Extrapolated elapsed time in seconds (valid when hasTime()).
     */
    float elapsedSec() const { return mElapsedSec; }

    /**
     * @brief   Track duration in seconds (valid when hasTime()).
     */
    float durationSec() const { return mDurationSec; }

private:

    const uint32_t mTicksPerSecond;

    std::string mTrackTitle;            ///< Raw track title from the snapshot.
    std::string mTrackArtist;           ///< Raw track artist from the snapshot.
    float mDurationSec { 0.0f };
    float mElapsedSec { 0.0f };
    float mRate { 1.0f };
    bool mElapsedKnown { false };       ///< A position is known (else hide bar/time).
    bool mPaused { false };             ///< Pause belief (reported or watch-set).
    bool mPlayerReportsPause { false }; ///< This player has ever reported paused.
    uint32_t mLastElapsedSeq { 0 };     ///< Last adopted elapsed freshness token.
    uint32_t mLastPlaybackSeq { 0 };    ///< Last seen PlaybackInfo freshness token.
    uint32_t mTickAccum { 0 };          ///< Ticks counted toward the next second.
    std::string mLastPlayer;            ///< Player name, to detect a player switch.
};

#endif // NOW_PLAYING_TRACKER_HPP
