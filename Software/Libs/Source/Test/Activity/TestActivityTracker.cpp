/**
 ******************************************************************************
 * @file    TestActivityTracker.cpp
 * @brief   Unit tests for Activity::ActivityTracker.
 *
 *          Pure-logic tests: no OS / sensor / FS dependency. Run
 *          on-device through Test_execute().
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestActTr"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/Activity/TestActivityTracker.hpp"
#include "Activity/Features/ActivityTracker.hpp"

namespace {

using Activity::ActivityTracker;

bool expectEq(uint32_t got, uint32_t want, const char* scenario)
{
    if (got == want) {
        return true;
    }
    LOG_ERROR("%s: got %u, want %u\n",
              scenario, (unsigned)got, (unsigned)want);
    return false;
}

// --- Cadence path ----------------------------------------------------------

// First tick after construction can never credit time - there is no
// previous tick to integrate against.
bool testFirstTickNoCredit()
{
    ActivityTracker t;
    return expectEq(t.tick(/*now*/ 0, /*isUserMoving*/ true, /*steps*/ 200),
                    0, "firstTickNoCredit");
}

// Two sustained cadence-path ticks 60 s apart -> +1 minute at score 1.0.
bool testTwoCadenceTicksAccumulate()
{
    ActivityTracker t;
    t.tick(0, true, 100);
    return expectEq(t.tick(60000, true, 100), 1,
                    "twoCadenceTicksAccumulate");
}

// Steps just below threshold -> no credit at all (and no HR evidence
// to fall back to).
bool testStepBelowThresholdInactive()
{
    ActivityTracker t;
    t.tick(0, true, 99);
    return expectEq(t.tick(60000, true, 99), 0,
                    "stepBelowThresholdInactive");
}

// Conservative integration: when the score changes between ticks we
// credit the LOWER of the two endpoint scores - we don't know when
// inside the gap the user's state actually changed.
bool testCadenceTransitionConservative()
{
    ActivityTracker t;
    t.tick(0,     true, 100);          // score 1.0
    t.tick(60000, true,   0);          // score 0.0 (min => no credit)
    // No credit yet.
    if (!expectEq(t.tick(120000, true, 0), 0, "trans@start")) return false;

    // Now sustain cadence again - credits the new gap fully.
    t.tick(180000, true, 100);         // bridge tick (min(0,1)=0, no credit)
    return expectEq(t.tick(240000, true, 100), 1, "trans@sustain");
}

// Sub-minute accumulation - many 30 s ticks integrate to whole
// minutes on the right boundary.
bool testSubMinuteAccumulation()
{
    ActivityTracker t;
    t.tick(0, true, 100);
    if (!expectEq(t.tick(30000, true, 100), 0, "subMinute@30s")) return false;
    return expectEq(t.tick(60000, true, 100), 1, "subMinute@60s");
}

// 60 small ticks at cadence-threshold should aggregate exactly to
// 1 minute (verifies no rounding loss in the integrator).
bool testManySmallTicks()
{
    ActivityTracker t;
    uint32_t last = 0;
    for (uint32_t i = 0; i <= 60; ++i) {
        last = t.tick(i * 1000u, true, 100);
    }
    return expectEq(last, 1, "manySmallTicks");
}

// --- HR + motion path ------------------------------------------------------

// HR-path is gated by a 3-sample evidence window. Two samples aren't
// enough no matter how elevated.
bool testHrPathRequiresFullWindow()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);

    t.tick(120000, true, 0);
    // Only 2 of 3 slots filled - hrPathQualified() must return false.
    // Ten 60s ticks at score 0 -> still 0 minutes.
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(120000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrPathRequiresFullWindow");
}

// Three high samples + motion -> HR-path credits at full rate (1.0),
// same as the cadence path. Ten 60 s ticks -> 10 * 60 s * 1.0 = 10 min.
// HR samples must keep arriving (every 2 min in LIGHT_ACTIVE) or the
// window expires after 5 min; the loop feeds a fresh sample every
// second tick to mirror real burst cadence.
bool testHrPathFullCredit()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);     // window full now

    t.tick(240000, /*isUserMoving*/ true, /*steps*/ 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        const uint32_t now = 240000 + i * 60000u;
        if ((i % 2) == 0) {
            // Mirror the ~2 min burst cadence in LA so the evidence
            // window never goes stale during the credit-accumulation.
            t.onHrSample(140, 2, now);
        }
        last = t.tick(now, true, 0);
    }
    return expectEq(last, 10, "hrPathFullCredit");
}

// 2 of 3 high samples is the quorum - still qualifies. Same staleness
// caveat applies; refresh the window through the loop.
bool testHrPathQuorumTwoOfThree()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);     // high
    t.onHrSample( 80, 2, 120000);     // low
    t.onHrSample(140, 2, 240000);     // high

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        const uint32_t now = 240000 + i * 60000u;
        if ((i % 2) == 0) {
            // High samples keep the quorum (2-of-3) satisfied as the
            // window slides.
            t.onHrSample(140, 2, now);
        }
        last = t.tick(now, true, 0);
    }
    // 10 * 60 s * 1.0 = 10 minutes
    return expectEq(last, 10, "hrPathQuorumTwoOfThree");
}

// 1 of 3 high is below quorum - doesn't qualify.
bool testHrPathBelowQuorum()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample( 80, 2, 120000);
    t.onHrSample( 80, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrPathBelowQuorum");
}

// Rate-limit: two samples 30 s apart -> only the first lands. The
// second is dropped, so window stays at 1 sample (not qualified).
bool testHrPathRateLimit()
{
    ActivityTracker t;
    t.onHrSample(140, 2,     0);   // accepted
    t.onHrSample(140, 2, 30000);   // dropped (< 120 s after first)
    t.onHrSample(140, 2, 60000);   // dropped
    t.onHrSample(140, 2, 90000);   // dropped

    // Only 1 sample in window -> qualified=false.
    t.tick(90000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(90000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrPathRateLimit");
}

// Trust-level filter: tl=0 samples are dropped (typical of PPG
// settling at burst start).
bool testHrPathTrustFilter()
{
    ActivityTracker t;
    t.onHrSample(140, 0,      0);     // tl too low, dropped
    t.onHrSample(140, 0, 120000);
    t.onHrSample(140, 0, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrPathTrustFilter");
}

// bpm == 0 samples are also dropped (data-glitch / no-contact).
bool testHrPathBpmZeroFilter()
{
    ActivityTracker t;
    t.onHrSample(  0, 2,      0);     // bpm 0, dropped
    t.onHrSample(  0, 2, 120000);
    t.onHrSample(  0, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrPathBpmZeroFilter");
}

// HR-path requires isUserMoving=true even if HR window qualifies.
// Sitting still with elevated HR (stress, illness) must NOT credit.
bool testHrPathRequiresMotion()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, /*isUserMoving*/ false, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, false, 0);
    }
    return expectEq(last, 0, "hrPathRequiresMotion");
}

// Stale window: if no fresh sample arrives for > 5 min, the evidence
// window is cleared. A subsequent qualifying setup would have to fill
// it again.
bool testHrPathStaleWindow()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    // Walk forward 10 minutes without any new HR sample. Each tick()
    // is supposed to check staleness and clear the window when the
    // newest sample is older than 5 min.
    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    // First few ticks may still see a fresh window (still within 5
    // min of the last sample); after that the window goes stale.
    // Either way we credit at most ~5 min * 0.7 = ~3 min. The exact
    // boundary depends on stale-check ordering inside tick(), so be
    // permissive: we just assert it's strictly less than the
    // never-stale baseline (7 min).
    if (last >= 7) {
        LOG_ERROR("hrPathStaleWindow: got %u, expected < 7\n", (unsigned)last);
        return false;
    }
    return true;
}

// Cadence path qualifies on steps alone, independent of the HR window.
// Both paths now credit at 1.0, so 10 minutes at cadence 100 -> 10 min
// whether or not the HR window would also qualify.
bool testCadencePathCreditsFully()
{
    ActivityTracker t;
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 100);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 100);
    }
    // Score 1.0 throughout -> full 10 min credited.
    return expectEq(last, 10, "cadencePathCreditsFully");
}

// --- Configurable HR cutoff ------------------------------------------------

// The HR cutoff tracks the value fed in from the user's HR zones.
// Raising it above the sample level means the window no longer
// qualifies: 140 bpm samples with a 150 bpm cutoff -> no credit.
bool testHrThresholdConfigurable()
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);

    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrThresholdConfigurable");
}

// A zero cutoff is ignored (a missing / uninitialised zone value must
// not drop the cutoff to 0 and qualify every sample). The prior value
// is kept: 140 < 150 -> still no credit.
bool testHrThresholdZeroIgnored()
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);
    t.setHrThresholdBpm(0);        // ignored -> stays 150

    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "hrThresholdZeroIgnored");
}

// Default cutoff is the moderate lower bound (95 bpm, ~50% HRmax), not
// the old fixed 110. 100 bpm samples (between the two) must qualify with
// no setter call -> would fail if the default were still 110.
bool testDefaultCutoffIsModerateLowerBound()
{
    ActivityTracker t;
    t.onHrSample(100, 2,      0);
    t.onHrSample(100, 2, 120000);
    t.onHrSample(100, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        const uint32_t now = 240000 + i * 60000u;
        if ((i % 2) == 0) {
            t.onHrSample(100, 2, now);
        }
        last = t.tick(now, true, 0);
    }
    return expectEq(last, 10, "defaultCutoffIsModerateLowerBound");
}

// reset() (e.g. midnight rollover) must preserve a configured cutoff,
// not revert it to the default.
bool testResetPreservesConfiguredThreshold()
{
    ActivityTracker t;
    t.setHrThresholdBpm(150);
    t.reset(/*baseline*/ 0, /*ts*/ 0);

    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "resetPreservesConfiguredThreshold");
}

// --- Baseline / reset -------------------------------------------------------

bool testBaselineCarriesThrough()
{
    ActivityTracker t;
    t.reset(/*baseline*/ 42, /*ts*/ 0);
    t.tick(0, true, 100);
    return expectEq(t.tick(60000, true, 100), 43,
                    "baselineCarriesThrough");
}

// reset() must clear both the accumulator AND the HR evidence
// window. After reset the next ticks start fresh - HR-path can't
// qualify until a new window is built up.
bool testResetClearsHrWindow()
{
    ActivityTracker t;
    // Build a qualified window then reset.
    t.onHrSample(140, 2,      0);
    t.onHrSample(140, 2, 120000);
    t.onHrSample(140, 2, 240000);

    t.reset(/*baseline*/ 0, /*ts*/ 240000);

    // Should now behave like a fresh tracker: no HR-path credit.
    t.tick(240000, true, 0);
    uint32_t last = 0;
    for (uint32_t i = 1; i <= 10; ++i) {
        last = t.tick(240000 + i * 60000u, true, 0);
    }
    return expectEq(last, 0, "resetClearsHrWindow");
}

} // namespace

void TestActivity_ActivityTracker_execute()
{
    LOG_INFO("ActivityTracker tests start\n");

    if (!testFirstTickNoCredit())            { LOG_ERROR("FAIL\n"); return; }
    if (!testTwoCadenceTicksAccumulate())    { LOG_ERROR("FAIL\n"); return; }
    if (!testStepBelowThresholdInactive())   { LOG_ERROR("FAIL\n"); return; }
    if (!testCadenceTransitionConservative()){ LOG_ERROR("FAIL\n"); return; }
    if (!testSubMinuteAccumulation())        { LOG_ERROR("FAIL\n"); return; }
    if (!testManySmallTicks())               { LOG_ERROR("FAIL\n"); return; }

    if (!testHrPathRequiresFullWindow())     { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathFullCredit())             { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathQuorumTwoOfThree())       { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathBelowQuorum())            { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathRateLimit())              { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathTrustFilter())            { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathBpmZeroFilter())          { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathRequiresMotion())         { LOG_ERROR("FAIL\n"); return; }
    if (!testHrPathStaleWindow())            { LOG_ERROR("FAIL\n"); return; }
    if (!testCadencePathCreditsFully())      { LOG_ERROR("FAIL\n"); return; }
    if (!testHrThresholdConfigurable())      { LOG_ERROR("FAIL\n"); return; }
    if (!testHrThresholdZeroIgnored())       { LOG_ERROR("FAIL\n"); return; }
    if (!testDefaultCutoffIsModerateLowerBound()) { LOG_ERROR("FAIL\n"); return; }
    if (!testResetPreservesConfiguredThreshold()) { LOG_ERROR("FAIL\n"); return; }

    if (!testBaselineCarriesThrough())       { LOG_ERROR("FAIL\n"); return; }
    if (!testResetClearsHrWindow())          { LOG_ERROR("FAIL\n"); return; }

    LOG_INFO("ActivityTracker tests PASS\n");
}
