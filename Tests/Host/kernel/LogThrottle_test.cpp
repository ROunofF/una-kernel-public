/**
 * @file LogThrottle_test.cpp
 * @brief Which failures of a repeating error get logged.
 *
 * The throttle is what stops one app that has stopped draining its queue from
 * logging two lines per sensor sample until the watchdog fires. What matters
 * is that a burst still announces itself, still reports how long it lasted,
 * stays throttled when failures and successes interleave, and never goes
 * permanently quiet - including across the clock wrap.
 */

#include <gtest/gtest.h>

#include "App/LogThrottle.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using App::LogThrottle;
using Action = LogThrottle::Action;

namespace {

constexpr uint32_t kWindow = 30000;

/// onSuccess() takes the clock as a callable; this pins it to a time.
auto at(uint32_t ms)
{
    return [ms] { return ms; };
}

}

TEST(LogThrottle, FirstFailureOfABurstIsLoggedInFull)
{
    LogThrottle t(kWindow);
    const auto d = t.onFailure(1000);
    EXPECT_EQ(d.action, Action::FIRST);
    EXPECT_EQ(d.suppressed, 0u);
}

TEST(LogThrottle, RepeatsInsideTheWindowAreQuiet)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    for (uint32_t ms = 1012; ms < 1000 + kWindow; ms += 12) {
        EXPECT_EQ(t.onFailure(ms).action, Action::QUIET) << "at " << ms;
    }
}

TEST(LogThrottle, SummaryAfterTheWindowCarriesTheUnloggedCount)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);                          // FIRST
    for (uint32_t i = 0; i < 7; ++i) {
        t.onFailure(1100 + i);                  // 7 quiet
    }
    const auto d = t.onFailure(1000 + kWindow); // exactly one window later
    EXPECT_EQ(d.action, Action::SUMMARY);
    EXPECT_EQ(d.suppressed, 7u);
}

TEST(LogThrottle, SummaryCountRestartsAfterEachSummary)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    t.onFailure(1001);
    t.onFailure(1002);
    EXPECT_EQ(t.onFailure(1000 + kWindow).suppressed, 2u);

    t.onFailure(1001 + kWindow);
    const auto d = t.onFailure(1000 + 2 * kWindow);
    EXPECT_EQ(d.action, Action::SUMMARY);
    EXPECT_EQ(d.suppressed, 1u);
}

TEST(LogThrottle, WindowIsMeasuredFromTheLastLoggedLine)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    EXPECT_EQ(t.onFailure(1000 + kWindow).action, Action::SUMMARY);
    // Not one window since the start - one window since the SUMMARY.
    EXPECT_EQ(t.onFailure(1000 + 2 * kWindow - 1).action, Action::QUIET);
    EXPECT_EQ(t.onFailure(1000 + 2 * kWindow).action, Action::SUMMARY);
}

TEST(LogThrottle, SuccessWithNoBurstReportsNothingAndReadsNoClock)
{
    LogThrottle t(kWindow);
    int reads = 0;
    EXPECT_EQ(t.onSuccess([&] { ++reads; return 5000u; }), 0u);
    EXPECT_EQ(reads, 0) << "the common path must not pay for a clock read";
}

TEST(LogThrottle, AlternatingFailureAndSuccessStaysThrottled)
{
    // A service drained slower than it is fed: its queue sits full and pushes
    // alternate. Ending the burst on any success would make every failure a
    // FIRST line - the storm again. Two minutes at 80 Hz in, 40 Hz out.
    LogThrottle t(kWindow);
    int firsts = 0;
    int summaries = 0;
    int recoveries = 0;
    for (uint32_t ms = 1000; ms < 1000 + 120000; ms += 25) {
        const auto d = t.onFailure(ms);
        if (d.action == Action::FIRST)   { ++firsts; }
        if (d.action == Action::SUMMARY) { ++summaries; }
        if (t.onSuccess(at(ms + 12)) != 0) { ++recoveries; }
    }
    EXPECT_EQ(firsts, 1);
    EXPECT_EQ(summaries, 3) << "one per 30 s window after the first line";
    EXPECT_EQ(recoveries, 0) << "it never went quiet, so it never recovered";
}

TEST(LogThrottle, SuccessSoonAfterAFailureDoesNotEndTheBurst)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    EXPECT_EQ(t.onSuccess(at(1000 + kWindow - 1)), 0u);
    EXPECT_EQ(t.onFailure(1000 + kWindow - 1).action, Action::QUIET)
        << "the burst is still running, so this is not a new FIRST";
}

TEST(LogThrottle, SuccessAWindowAfterTheLastFailureReportsTheWholeBurst)
{
    LogThrottle t(kWindow);
    uint32_t last = 0;
    for (uint32_t i = 0; i < 955; ++i) {
        last = 1000 + i * 12;
        t.onFailure(last);
    }
    EXPECT_EQ(t.onSuccess(at(last + kWindow)), 955u);
    EXPECT_EQ(t.onSuccess(at(last + kWindow + 1)), 0u)
        << "the burst is over; a second success is quiet";
}

TEST(LogThrottle, SingleFailureRecoversSilently)
{
    // It was already logged in full; "recovered after 1" would be noise.
    LogThrottle t(kWindow);
    t.onFailure(1000);
    EXPECT_EQ(t.onSuccess(at(1000 + kWindow)), 0u);
    EXPECT_EQ(t.onFailure(1000 + kWindow + 1).action, Action::FIRST)
        << "but the burst did end";
}

TEST(LogThrottle, ANewBurstStartsWithAFullLine)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    t.onFailure(1001);
    EXPECT_EQ(t.onSuccess(at(1001 + kWindow)), 2u);

    const auto d = t.onFailure(1002 + kWindow);
    EXPECT_EQ(d.action, Action::FIRST);
    EXPECT_EQ(t.onSuccess(at(1002 + 2 * kWindow)), 0u)
        << "the new burst holds one failure, not three";
}

TEST(LogThrottle, SummaryIsNotInheritedAcrossBursts)
{
    LogThrottle t(kWindow);
    t.onFailure(1000);
    t.onFailure(1001);
    t.onFailure(1002);
    t.onSuccess(at(1002 + kWindow));

    const uint32_t start = 1003 + kWindow;
    t.onFailure(start);                         // FIRST of the new burst
    const auto d = t.onFailure(start + kWindow);
    EXPECT_EQ(d.action, Action::SUMMARY);
    EXPECT_EQ(d.suppressed, 0u) << "the old burst's quiet failures do not carry over";
}

TEST(LogThrottle, FailuresResumingAfterSilenceReportTheTail)
{
    // No success in between: the quiet failures at the end of the first run
    // are carried by the summary that the next failure logs.
    LogThrottle t(kWindow);
    t.onFailure(1000);                          // FIRST
    t.onFailure(1010);                          // quiet
    t.onFailure(1020);                          // quiet
    const auto d = t.onFailure(600000);         // ten minutes later
    EXPECT_EQ(d.action, Action::SUMMARY);
    EXPECT_EQ(d.suppressed, 2u);
}

TEST(LogThrottle, ClockWrapLogsEarlyButNeverSuppressesForever)
{
    // OS::GetTimeMs() wraps at 4,194,304,000 ms, not 2^32. Across that wrap the
    // unsigned difference is huge rather than small, so the effect is one
    // summary early - never a burst that goes quiet for good.
    constexpr uint32_t kWrap = 4194304000u;
    LogThrottle t(kWindow);
    t.onFailure(kWrap - 10);
    const auto d = t.onFailure(5);              // 15 ms later, after the wrap
    EXPECT_EQ(d.action, Action::SUMMARY);

    EXPECT_EQ(t.onFailure(6).action, Action::QUIET)
        << "and the window resumes from the new timestamp";
}

TEST(LogThrottle, ShortGapAcrossA32BitWrapStaysInsideTheWindow)
{
    LogThrottle t(kWindow);
    t.onFailure(0xFFFFFFF0u);
    EXPECT_EQ(t.onFailure(0x10u).action, Action::QUIET)
        << "32 ms across a 2^32 wrap is inside the window";
}

TEST(LogThrottle, ClockAppearingToRunBackwardsIsIgnored)
{
    // A caller preempted between reading the clock and calling in can arrive
    // with a time a little before the last one logged. That must not read as
    // a gap of nearly 2^32 ms - no early summary, and no backwards timestamp.
    LogThrottle t(kWindow);
    t.onFailure(1000000);
    EXPECT_EQ(t.onFailure(999990).action, Action::QUIET);
    EXPECT_EQ(t.onFailure(1000000 + kWindow - 1).action, Action::QUIET)
        << "the window still runs from 1000000, not from 999990";
    EXPECT_EQ(t.onFailure(1000000 + kWindow).action, Action::SUMMARY);
}

TEST(LogThrottle, TimeZeroIsAUsableTimestamp)
{
    // Zero is the no-burst marker internally; a failure at t=0 must still
    // open a burst rather than leave the throttle looking idle.
    LogThrottle t(kWindow);
    EXPECT_EQ(t.onFailure(0).action, Action::FIRST);
    EXPECT_EQ(t.onFailure(1).action, Action::QUIET);
}

TEST(LogThrottle, ConcurrentFailuresAtARealisticTimeLogOneFirstLine)
{
    // Threads failing together at the start of a burst: exactly one wins the
    // FIRST line and none sees a stale timestamp and logs a bogus summary.
    // Run at a realistic uptime, where a stale zero would look long ago.
    constexpr uint32_t kNow = 10000000;
    LogThrottle t(kWindow);
    constexpr int kThreads = 8;
    constexpr int kEach    = 2000;
    std::atomic<int> firsts {0};
    std::atomic<int> summaries {0};

    std::vector<std::thread> pool;
    for (int n = 0; n < kThreads; ++n) {
        pool.emplace_back([&] {
            for (int i = 0; i < kEach; ++i) {
                const auto d = t.onFailure(kNow);
                if (d.action == Action::FIRST)   { ++firsts; }
                if (d.action == Action::SUMMARY) { ++summaries; }
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }

    EXPECT_EQ(firsts.load(), 1);
    EXPECT_EQ(summaries.load(), 0);
    EXPECT_EQ(t.onSuccess(at(kNow + kWindow)), static_cast<uint32_t>(kThreads * kEach))
        << "with no success racing, every failure is counted";
}
