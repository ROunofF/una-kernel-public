// Host unit tests for NowPlayingTracker: the pause-belief + local elapsed
// extrapolation that used to live (untested) in MediaControlView. These lock the
// exact behaviour so the view can be reduced to a thin renderer over it.

#include <gtest/gtest.h>

#include "Components/Shared/NowPlayingTracker.hpp"

namespace {

// A snapshot that reports a fresh PlaybackInfo (state, and optionally elapsed).
NowPlaying playerSnapshot(const char* name, NowPlaying::State state,
        uint32_t playbackSeq)
{
    NowPlaying np {};
    np.playerName = name;
    np.state = state;
    np.playbackSeq = playbackSeq;
    return np;
}

// Advance the tracker by a whole second worth of ticks; returns how many ticks
// reported a move (should be exactly one, on the last tick).
int tickOneSecond(NowPlayingTracker& t, uint32_t ticksPerSecond)
{
    int moves = 0;
    for (uint32_t i = 0; i < ticksPerSecond; i++) {
        if (t.onTick()) {
            moves++;
        }
    }
    return moves;
}

constexpr uint32_t kTps = 4;   // ticks per second (small, for fast tests)

} // namespace

TEST(NowPlayingTracker, ExposesRawTrackFieldsAndPlayerName)
{
    NowPlayingTracker t(kTps);

    NowPlaying np {};
    np.playerName = "Spotify";
    np.artist = "Pink Floyd";
    np.title = "Comfortably Numb";
    t.onSnapshot(&np);
    EXPECT_EQ(t.trackTitle(), "Comfortably Numb");
    EXPECT_EQ(t.trackArtist(), "Pink Floyd");
    EXPECT_EQ(t.playerName(), "Spotify");

    // No track title: it stays empty (the view falls back to the player name for
    // the top line, not the artist - the artist has its own line).
    np.title.clear();
    t.onSnapshot(&np);
    EXPECT_EQ(t.trackTitle(), "");
    EXPECT_EQ(t.trackArtist(), "Pink Floyd");
    EXPECT_EQ(t.playerName(), "Spotify");

    // Null clears the track fields.
    t.onSnapshot(nullptr);
    EXPECT_EQ(t.trackTitle(), "");
    EXPECT_EQ(t.trackArtist(), "");
}

TEST(NowPlayingTracker, AdoptsElapsedOnlyWhenSeqChanges)
{
    NowPlayingTracker t(kTps);

    NowPlaying np = playerSnapshot("P", NowPlaying::State::Playing, 1);
    np.durationSec = 100.0f;
    np.elapsedSec = 10.0f;
    np.elapsedSeq = 1;
    t.onSnapshot(&np);
    ASSERT_TRUE(t.hasTime());
    EXPECT_FLOAT_EQ(t.elapsedSec(), 10.0f);

    // Extrapolate one second: 10 -> 11.
    EXPECT_EQ(tickOneSecond(t, kTps), 1);
    EXPECT_FLOAT_EQ(t.elapsedSec(), 11.0f);

    // A snapshot carrying the SAME elapsedSeq (e.g. a volume change re-emit) must
    // not reset the extrapolated position back to 10.
    np.playbackSeq = 2;          // fresh playback report...
    np.elapsedSec = 10.0f;       // ...still carrying the last elapsed
    np.elapsedSeq = 1;           // same token -> not fresh
    t.onSnapshot(&np);
    EXPECT_FLOAT_EQ(t.elapsedSec(), 11.0f);

    // A new elapsedSeq is adopted.
    np.playbackSeq = 3;
    np.elapsedSec = 50.0f;
    np.elapsedSeq = 2;
    t.onSnapshot(&np);
    EXPECT_FLOAT_EQ(t.elapsedSec(), 50.0f);
}

TEST(NowPlayingTracker, ExtrapolatesOncePerSecondAndClamps)
{
    NowPlayingTracker t(kTps);

    NowPlaying np = playerSnapshot("P", NowPlaying::State::Playing, 1);
    np.durationSec = 3.0f;
    np.elapsedSec = 0.0f;
    np.elapsedSeq = 1;
    t.onSnapshot(&np);

    // Fewer than ticksPerSecond ticks -> no move.
    for (uint32_t i = 0; i < kTps - 1; i++) {
        EXPECT_FALSE(t.onTick());
    }
    EXPECT_TRUE(t.onTick());              // the tick that completes the second
    EXPECT_FLOAT_EQ(t.elapsedSec(), 1.0f);

    tickOneSecond(t, kTps);               // 2.0
    tickOneSecond(t, kTps);               // 3.0 (== duration)
    EXPECT_FLOAT_EQ(t.elapsedSec(), 3.0f);

    // Clamped at duration: further seconds do not exceed it.
    tickOneSecond(t, kTps);
    EXPECT_FLOAT_EQ(t.elapsedSec(), 3.0f);
}

TEST(NowPlayingTracker, RespectsReportedRate)
{
    NowPlayingTracker t(kTps);

    NowPlaying np = playerSnapshot("P", NowPlaying::State::Playing, 1);
    np.durationSec = 100.0f;
    np.elapsedSec = 0.0f;
    np.elapsedSeq = 1;
    np.rate = 2.0f;
    t.onSnapshot(&np);

    tickOneSecond(t, kTps);
    EXPECT_FLOAT_EQ(t.elapsedSec(), 2.0f);   // advanced by rate, not 1.0
}

TEST(NowPlayingTracker, PausedFreezesExtrapolation)
{
    NowPlayingTracker t(kTps);

    NowPlaying np = playerSnapshot("P", NowPlaying::State::Paused, 1);
    np.durationSec = 100.0f;
    np.elapsedSec = 5.0f;
    np.elapsedSeq = 1;
    t.onSnapshot(&np);
    EXPECT_TRUE(t.paused());

    EXPECT_EQ(tickOneSecond(t, kTps), 0);    // no move while paused
    EXPECT_FLOAT_EQ(t.elapsedSec(), 5.0f);
}

TEST(NowPlayingTracker, ReportedPauseThenPlayTogglesBelief)
{
    NowPlayingTracker t(kTps);

    NowPlaying paused = playerSnapshot("P", NowPlaying::State::Paused, 1);
    t.onSnapshot(&paused);
    EXPECT_TRUE(t.paused());

    NowPlaying playing = playerSnapshot("P", NowPlaying::State::Playing, 2);
    t.onSnapshot(&playing);
    EXPECT_FALSE(t.paused());   // player reports pause, so "playing" is trusted
}

TEST(NowPlayingTracker, PlayingReportCannotClearWatchPauseForNonReportingPlayer)
{
    // VLC-style: the player only ever reports "playing". A watch-set pause must
    // survive its bogus "playing" reports.
    NowPlayingTracker t(kTps);

    NowPlaying playing = playerSnapshot("VLC", NowPlaying::State::Playing, 1);
    t.onSnapshot(&playing);
    EXPECT_FALSE(t.paused());

    EXPECT_TRUE(t.onTogglePause());   // watch pauses
    EXPECT_TRUE(t.paused());

    NowPlaying playing2 = playerSnapshot("VLC", NowPlaying::State::Playing, 2);
    t.onSnapshot(&playing2);
    EXPECT_TRUE(t.paused());          // still paused: player never reported pause
}

TEST(NowPlayingTracker, StalePlaybackSeqDoesNotChangeBelief)
{
    NowPlayingTracker t(kTps);

    NowPlaying paused = playerSnapshot("P", NowPlaying::State::Paused, 1);
    t.onSnapshot(&paused);
    EXPECT_TRUE(t.paused());

    // A re-emit carrying "playing" but the SAME playbackSeq (e.g. a volume
    // change) must be ignored - the belief stays paused.
    NowPlaying reemit = playerSnapshot("P", NowPlaying::State::Playing, 1);
    reemit.volume = 0.5f;
    t.onSnapshot(&reemit);
    EXPECT_TRUE(t.paused());
}

TEST(NowPlayingTracker, PlayerSwitchResetsBeliefAndLearning)
{
    NowPlayingTracker t(kTps);

    NowPlaying a = playerSnapshot("A", NowPlaying::State::Paused, 1);
    t.onSnapshot(&a);
    EXPECT_TRUE(t.paused());

    // New player, reports "playing". The switch clears the belief AND the
    // pause-learning, so this "playing" leaves it not paused.
    NowPlaying b = playerSnapshot("B", NowPlaying::State::Playing, 2);
    t.onSnapshot(&b);
    EXPECT_FALSE(t.paused());

    // And because learning was reset, a watch pause on B now survives B's
    // subsequent "playing" reports (B has not reported pause).
    EXPECT_TRUE(t.onTogglePause());
    NowPlaying b2 = playerSnapshot("B", NowPlaying::State::Playing, 3);
    t.onSnapshot(&b2);
    EXPECT_TRUE(t.paused());
}

TEST(NowPlayingTracker, SkipClearsPauseBelief)
{
    NowPlayingTracker t(kTps);

    NowPlaying paused = playerSnapshot("P", NowPlaying::State::Paused, 1);
    t.onSnapshot(&paused);
    EXPECT_TRUE(t.paused());

    t.onSkip();
    EXPECT_FALSE(t.paused());
}

TEST(NowPlayingTracker, NullSnapshotHidesTimeKeepsPause)
{
    NowPlayingTracker t(kTps);

    NowPlaying np = playerSnapshot("P", NowPlaying::State::Paused, 1);
    np.durationSec = 100.0f;
    np.elapsedSec = 5.0f;
    np.elapsedSeq = 1;
    t.onSnapshot(&np);
    ASSERT_TRUE(t.hasTime());
    ASSERT_TRUE(t.paused());

    t.onSnapshot(nullptr);
    EXPECT_FALSE(t.hasTime());   // position hidden
    EXPECT_TRUE(t.paused());     // belief preserved
}

TEST(NowPlayingTracker, MissingElapsedHidesTimeKeepsBelief)
{
    NowPlayingTracker t(kTps);

    // A player that reports pause state but no position (stream).
    NowPlaying np = playerSnapshot("P", NowPlaying::State::Paused, 1);
    // no durationSec, no elapsedSec
    t.onSnapshot(&np);

    EXPECT_FALSE(t.hasTime());
    EXPECT_TRUE(t.paused());
}
