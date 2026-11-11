/**
 ******************************************************************************
 * @file    DailyLedger.hpp
 * @brief   Pure running-day ledger for the Daily Health subsystem.
 *
 *          Owns the in-progress day: its accumulated metrics, calendar
 *          date, dirty flag and the delta-accumulation baseline. Holds
 *          all the transition logic - boot restore, per-minute delta
 *          integration and day rollover ("closing the books").
 *
 *          Pure by construction: no OS, no FreeRTOS, no I/O, no
 *          observers, no locking. The owner (DailyHealth::Service)
 *          wraps it with the worker thread, timers, persistence and
 *          observer fan-out, and serialises access under its own mutex.
 *          That split makes every transition trivially unit-testable.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_DAILY_LEDGER_HPP
#define COMPONENT_DAILY_HEALTH_DAILY_LEDGER_HPP

#include <cstdint>
#include <ctime>

#include "DailyHealth/Types.hpp"

namespace DailyHealth
{

class DailyLedger
{
public:
    /// Cumulative-from-boot counters sampled from the activity data
    /// source. A plain struct so the ledger needs no IActivityDataSource
    /// dependency - the owner samples the source and hands a snapshot.
    struct Snapshot
    {
        uint32_t steps;
        uint32_t floorsUp;
        uint32_t floorsDown;
        uint32_t activityMinutes;
        float    avgHr;
    };

    /// Result of one minute tick.
    struct TickOutcome
    {
        /// A day boundary was crossed during this tick, or the first
        /// tick with a valid clock found the restored day belongs to an
        /// earlier date. The ledger has already closed the finishing day
        /// and opened a fresh one; the owner must archive
        /// finishedRecord() / finishedMetrics() at finishedTm() and
        /// erase the recovery file.
        bool        dayRolled;
        /// Current-day record after this tick (HR averaged up to `now`).
        /// On a rollover this is the freshly-opened day, not the closed
        /// one.
        DailyRecord publish;
    };

    /// Result of a boot-time restore decision.
    struct RestoreOutcome
    {
        /// A recovery file from an earlier day was found. The owner must
        /// archive finishedRecord() / finishedMetrics() at finishedTm()
        /// and erase the recovery file. The ledger has already opened a
        /// fresh day for `today`.
        bool archiveStale;
    };

    // ---- lifecycle ---------------------------------------------------------

    /// Open a brand-new empty day. Leaves the delta baseline untouched.
    void startDay(const DailyDate& today);

    /// Decide how to come up at boot given what storage returned.
    ///
    /// @param today        the date the watch booted on
    /// @param hadRecovery  storage.loadCurrent() succeeded
    /// @param recovered    the recovered metrics (ignored if !hadRecovery)
    /// @param recoveredTm  the date the recovery belongs to
    /// @param clockValid   the wall clock has been set since the RTC
    ///                     last lost power, so @p today can be trusted
    ///
    /// The delta baseline is left at its constructed zero: the source
    /// also restarts from 0 after a reboot, so the first tick credits
    /// the boot-to-now contribution without double-counting.
    ///
    /// With an unset clock (a PMIC power cycle resets the RTC to
    /// 2000-01-01) @p today is meaningless, so nothing is archived: the
    /// recovery is adopted as the current day with its date held
    /// unconfirmed, and the first tick with a valid clock settles it.
    RestoreOutcome bootRestore(const DailyDate&    today,
                               bool                hadRecovery,
                               const DailyMetrics& recovered,
                               const std::tm&      recoveredTm,
                               bool                clockValid = true);

    /// Integrate one minute. `now` drives both the HR-slot index and the
    /// day-rollover check; `s` carries the cumulative-from-boot counters.
    /// The crossing-minute scalar delta is credited to the day being
    /// closed. On a day boundary the ledger closes the finishing day,
    /// exposes it via the finished* accessors for the owner to archive,
    /// and immediately opens a fresh day - the owner does not call
    /// startDay() itself.
    ///
    /// While @p clockValid is false the tick neither dates nor books
    /// anything: the baseline is held so the counts gathered meanwhile
    /// are credited, in one delta, by the first tick with a valid clock.
    /// That tick first settles the unconfirmed date - an earlier day is
    /// closed and reported through dayRolled exactly like a rollover.
    TickOutcome onMinuteTick(const std::tm& now, const Snapshot& s,
                             bool clockValid = true);

    /// Factory reset: empty `today` and re-anchor the delta baseline to
    /// the source's current readings. The source is monotonic and
    /// cannot itself be zeroed, so the next tick credits a zero delta.
    /// With an unset clock the fresh day's date stays unconfirmed.
    void resetTo(const DailyDate& today, const Snapshot& s,
                 bool clockValid = true);

    // ---- accessors ---------------------------------------------------------

    /// Today's record. @p now is the current wall-clock time; HR is
    /// averaged only over minutes up to it. Pass the actual current
    /// time, not the day-open time - otherwise HR recorded after the
    /// day was opened is silently excluded from AHR/RHR. While the date
    /// is unconfirmed @p now is ignored: HR covers the whole day and the
    /// counts not yet booked are included.
    DailyRecord         composeRecord(const std::tm& now) const;
    const DailyMetrics& metrics() const { return mCurrent; }
    const DailyDate&    date()    const { return mDate; }
    /// False while the day's date waits on the clock being set.
    bool                dateKnown() const { return mDateKnown; }
    bool                dirty()   const { return mDirty; }
    void                clearDirty()    { mDirty = false; }

    /// The just-closed day. Valid after onMinuteTick() reported
    /// dayRolled or bootRestore() reported archiveStale, until the next
    /// startDay() / onMinuteTick().
    const DailyMetrics& finishedMetrics() const { return mFinished; }
    const DailyRecord&  finishedRecord()  const { return mFinishedRecord; }
    const std::tm&      finishedTm()      const { return mFinishedTm; }

private:
    DailyMetrics mCurrent{};
    DailyDate    mDate{};
    bool         mDirty{false};

    // mDate is unconfirmed: the clock was unset when the day was opened.
    // mProvisional marks that mCurrent holds a restored day, which must
    // be archived if the clock turns out to be on a different date.
    bool         mDateKnown{true};
    bool         mProvisional{false};

    // Counts gathered since the baseline while the date is unconfirmed,
    // not yet booked to any day. Only feed composeRecord(). Held in RAM
    // only, so a second power cycle before the clock is set loses them
    // (the restored day itself survives), and a watch that never syncs
    // with a phone, the only source of the time zone, never books them.
    Snapshot     mPending{};

    // Delta baseline: last cumulative readings seen from the source.
    // Both the ledger and the source start at 0 after a reboot, so the
    // first tick credits the boot-to-now contribution without double-
    // counting.
    uint32_t mLastSteps{0};
    uint32_t mLastFloorsUp{0};
    uint32_t mLastFloorsDown{0};
    uint32_t mLastActivityMinutes{0};

    // Just-closed-day handoff. Members rather than return-by-value to
    // keep the ~1.5 KB DailyMetrics off the owner's (worker) stack.
    DailyMetrics mFinished{};
    DailyRecord  mFinishedRecord{};
    std::tm      mFinishedTm{};
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_DAILY_LEDGER_HPP
