#include <gtest/gtest.h>

#include "Activity/Features/MotionTracker.hpp"

// Regression cover for the Activity-Minutes over-count bug: the BMI270
// motion-detect sub-sensor re-publishes SIG_MOTION as a ~5 s heartbeat for
// the whole duration of sustained motion (ImuSubSensorMotionDetect), and it
// is fed straight into MotionTracker::onUpdate(true, ...). Those same-state
// republishes must be treated as non-edges so they do not reset the dwell
// clock; otherwise isMotion()'s "continuously detected for >= N s" check can
// never be satisfied (it kept under-reporting motion / mis-gating the
// activity-minute HR path). MotionTracker is a pure leaf class, so we test
// the contract directly.

using Activity::MotionTracker;

namespace
{
// isMotion()/isNoMotion() use a strict ">" comparison against sec*1000.
constexpr uint32_t kEdge = 1000;  // ms; the rising-edge timestamp
} // namespace

// --- The regression: heartbeat republishes must NOT reset the dwell clock ---

TEST(MotionTracker, SigMotionHeartbeatDoesNotResetDwellClock)
{
    MotionTracker m;

    m.onUpdate(false, 0);          // initial: no motion
    m.onUpdate(true,  kEdge);      // rising edge -> motion starts

    // Sustained-motion heartbeat: SIG_MOTION republished every ~5 s, each
    // arriving as onUpdate(true, ...). None of these is a state change.
    for (uint32_t t = kEdge + 5000; t <= 41000; t += 5000) {
        m.onUpdate(true, t);
    }

    // 41 s after the *edge*, with the most recent heartbeat only 1 s ago.
    //  - Correct (clock anchored to the edge @1000): 41000 > 30000 -> true.
    //  - Buggy  (clock reset by last heartbeat @41000): 1000 > 30000 -> false.
    EXPECT_TRUE(m.isMotion(30, 42000));
    EXPECT_TRUE(m.raw());
}

// --- isMotion() honours the "sustained for >= N s" boundary -----------------

TEST(MotionTracker, IsMotionRequiresSustainedDuration)
{
    MotionTracker m;

    m.onUpdate(false, 0);
    m.onUpdate(true, kEdge);       // edge at t=1000

    EXPECT_FALSE(m.isMotion(30, kEdge));            // 0 s elapsed
    EXPECT_FALSE(m.isMotion(30, kEdge + 30000));    // exactly 30 s: not yet (strict >)
    EXPECT_TRUE (m.isMotion(30, kEdge + 30001));    // just past 30 s
}

TEST(MotionTracker, IsMotionFalseWhenNotDetected)
{
    MotionTracker m;

    m.onUpdate(false, 0);
    EXPECT_FALSE(m.isMotion(30, 1'000'000));        // never moved
}

// --- Falling edge + isNoMotion ----------------------------------------------

TEST(MotionTracker, NoMotionMeasuredFromFallingEdge)
{
    MotionTracker m;

    m.onUpdate(false, 0);
    m.onUpdate(true,  kEdge);       // moving
    m.onUpdate(false, 50000);       // NO_MOTION falling edge at t=50000

    EXPECT_FALSE(m.isMotion(30, 90000));            // detected is false now
    EXPECT_FALSE(m.isNoMotion(180, 50000 + 180000));// exactly 180 s: not yet
    EXPECT_TRUE (m.isNoMotion(180, 50000 + 180001));// just past 180 s
}

TEST(MotionTracker, RepeatedNoMotionDoesNotResetStillnessClock)
{
    MotionTracker m;

    m.onUpdate(true,  kEdge);
    m.onUpdate(false, 50000);       // falling edge
    m.onUpdate(false, 55000);       // same-state republish (defensive)
    m.onUpdate(false, 60000);       // same-state republish

    //  - Correct (clock anchored to the edge @50000): 180001 > 180000 -> true.
    //  - Buggy  (clock reset by last @60000): 170001 > 180000 -> false.
    EXPECT_TRUE(m.isNoMotion(180, 50000 + 180001));
    EXPECT_FALSE(m.raw());
}

// --- First sample seeds the clock (no measuring from time 0) -----------------

TEST(MotionTracker, FirstSampleSeedsTheClock)
{
    MotionTracker m;

    m.onUpdate(false, 10000);       // first sample at t=10000, not 0

    // Stillness is measured from the seed (10000), not from epoch 0.
    EXPECT_FALSE(m.isNoMotion(5, 14000));           // 4 s since seed
    EXPECT_TRUE (m.isNoMotion(5, 15001));           // just past 5 s since seed
}
