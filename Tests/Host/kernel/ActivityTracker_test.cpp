#include <gtest/gtest.h>

#include "Activity/Features/ActivityTracker.hpp"

// Defensive cover for the activity-minute integrator. tick() credits the
// wall-clock gap since the previous tick, scaled by the score. Ticks are
// driven by the Activity FSM's 30 s periodic timer plus rising edges, so a
// legitimate gap is ~30 s. If ticks ever stop being delivered for a long
// stretch (thread stall, a deep-sleep state that dropped the FSM timer as a
// wake source, a tick-restore glitch), the next tick must NOT book the whole
// unobserved gap as active time. These tests pin that clamp, plus the normal
// crediting and the conservative min-gate that surrounds it.
//
// The cadence path (stepsLastMin >= 100 -> score 1.0) is used to drive a
// known score without touching the HR-evidence window.

using Activity::ActivityTracker;

namespace
{
constexpr uint32_t kActiveSpm = 100;   // >= cadence threshold -> score 1.0
} // namespace

// --- Normal, short gaps are credited in full --------------------------------

TEST(ActivityTracker, NormalGapsCreditedFully)
{
    ActivityTracker t;

    t.tick(0,      false, kActiveSpm);   // seed (first tick credits nothing)
    t.tick(60000,  false, kActiveSpm);   // +60 s
    const uint32_t mins = t.tick(120000, false, kActiveSpm); // +60 s

    // 120 s of credited motion -> 2 active minutes, untouched by the cap.
    EXPECT_EQ(mins, 2u);
}

// --- The regression: an over-cap gap is clamped, not fully credited ---------

TEST(ActivityTracker, OverCapGapIsClamped)
{
    ActivityTracker t;

    t.tick(0, false, kActiveSpm);                       // seed, score 1.0
    const uint32_t mins = t.tick(3'600'000, false, kActiveSpm); // 1 h gap

    // Clamped to skMaxCreditableGapMs (90 s) -> 90000/60000 = 1 minute.
    //  - Without the cap this would be 3'600'000/60000 = 60 minutes.
    EXPECT_EQ(mins, 1u);
}

// --- The conservative min-gate still gates a gap on BOTH endpoints ----------

TEST(ActivityTracker, InactiveEndpointCreditsNothing)
{
    ActivityTracker t;

    t.tick(0, false, kActiveSpm);            // active
    const uint32_t mins = t.tick(3'600'000, false, 0); // long gap, now inactive

    // min(prevScore=1.0, score=0.0) == 0 -> no credit at all, cap irrelevant.
    EXPECT_EQ(mins, 0u);
}

// --- The HR + motion path credits at full rate (1.0), same as cadence -------

TEST(ActivityTracker, HrPathCreditedFully)
{
    ActivityTracker t;

    // Fill the 3-slot evidence window with elevated, trusted samples
    // (default cutoff is 95 bpm; 140 >= 95). Samples are rate-limited to
    // one per ~2 min, so space them 120 s apart.
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);   // window full

    t.tick(240000, /*moving*/ true, /*steps*/ 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        const uint32_t now = 240000 + i * 60000u;
        if ((i % 2) == 0) {
            t.onHrSample(140, 2, now);   // keep the window fresh (< 5 min)
        }
        last = t.tick(now, true, 0);
    }

    // 10 * 60 s at full credit -> 10 minutes (was 7 under the old 0.7).
    EXPECT_EQ(last, 10u);
}

// --- The HR cutoff is runtime-configurable from the user's HR zones ---------

TEST(ActivityTracker, HrThresholdConfigurable)
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);   // raise the cutoff above the sample level

    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }

    // 140 < 150 -> no elevated readings -> no HR-path credit.
    EXPECT_EQ(last, 0u);
}

// A zero cutoff is ignored so an unset zone value can't qualify everything.

TEST(ActivityTracker, HrThresholdZeroIgnored)
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);
    t.setHrThresholdBpm(0);     // ignored -> stays 150

    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }

    // 140 < 150 proves the zero did not lower the cutoff.
    EXPECT_EQ(last, 0u);
}

// --- The default cutoff is 95 bpm (WHO ~50% HRmax), not the old 110 ---------

TEST(ActivityTracker, DefaultCutoffIsModerateLowerBound)
{
    ActivityTracker t;   // no setHrThresholdBpm -> default cutoff

    // 100 bpm sits between the new default (95) and the old fixed 110.
    // It must now qualify: proves the default dropped to the zone-1
    // upper bound and would fail if it were still 110.
    t.onHrSample(100, 2,      0);
    t.onHrSample(100, 2, 120000);
    t.onHrSample(100, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        const uint32_t now = 240000 + i * 60000u;
        if ((i % 2) == 0) {
            t.onHrSample(100, 2, now);   // keep the window fresh
        }
        last = t.tick(now, true, 0);
    }

    EXPECT_EQ(last, 10u);   // 100 >= 95 -> full credit
}

// --- reset() must NOT revert a configured cutoff to the default -------------

TEST(ActivityTracker, ResetPreservesConfiguredThreshold)
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);        // configured cutoff
    t.reset(/*baseline*/ 0, /*ts*/ 0);   // e.g. midnight rollover

    // Still 150 after reset: 140 bpm samples must not qualify.
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }

    EXPECT_EQ(last, 0u);   // 140 < 150 -> cutoff survived the reset
}
