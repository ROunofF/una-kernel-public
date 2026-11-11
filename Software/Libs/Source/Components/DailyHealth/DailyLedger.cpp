/**
 ******************************************************************************
 * @file    DailyLedger.cpp
 ******************************************************************************
 */

#include "DailyHealth/DailyLedger.hpp"

#include <cstdint>

#include "DailyHealth/HealthCalculator.hpp"

namespace DailyHealth
{

namespace {

// Cumulative - last_seen, saturating to 0 on rollback. With a monotonic
// source the rollback guard never triggers; it is purely defensive.
// Advances @p last to @p cur as a side effect.
uint32_t takeDelta(uint32_t cur, uint32_t& last)
{
    const uint32_t d = (cur >= last) ? (cur - last) : 0u;
    last = cur;
    return d;
}

uint32_t addSatU32(uint32_t base, uint32_t add)
{
    const uint64_t sum = static_cast<uint64_t>(base) + add;
    return (sum > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(sum);
}

uint16_t addSatU16(uint16_t base, uint32_t add)
{
    const uint32_t sum = static_cast<uint32_t>(base) + add;
    return (sum > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(sum);
}

// takeDelta() without advancing the baseline.
uint32_t peekDelta(uint32_t cur, uint32_t last)
{
    return (cur >= last) ? (cur - last) : 0u;
}

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

void DailyLedger::startDay(const DailyDate& today)
{
    mCurrent = DailyMetrics{};
    mDate    = today;
    mDirty   = false;
}

DailyLedger::RestoreOutcome DailyLedger::bootRestore(const DailyDate&    today,
                                                     bool                hadRecovery,
                                                     const DailyMetrics& recovered,
                                                     const std::tm&      recoveredTm,
                                                     bool                clockValid)
{
    RestoreOutcome out{};

    if (!clockValid) {
        // Nothing to compare the recovery against yet. Keep it as the
        // current day under its own date, unconfirmed; onMinuteTick()
        // archives it if the clock comes up on a different day.
        if (hadRecovery) {
            mCurrent = recovered;
            mDate    = DailyDate::fromTm(recoveredTm);
            mDirty   = false;
        } else {
            startDay(today);
        }
        mDateKnown   = false;
        mProvisional = hadRecovery;
        return out;
    }

    if (hadRecovery && DailyDate::fromTm(recoveredTm).sameDay(today)) {
        // Recovery is for today - adopt it as the in-progress day.
        mCurrent = recovered;
        mDate    = today;
        mDirty   = false;
        return out;
    }

    if (hadRecovery) {
        // Recovery belongs to an earlier day - hand it back for
        // archiving as a finalised record.
        mFinished       = recovered;
        mFinishedTm     = recoveredTm;
        mFinishedRecord = HealthCalculator::compute(mFinished, mFinishedTm,
                                                    /*upToNow*/ false);
        out.archiveStale = true;
    }

    startDay(today);
    return out;
}

// ============================================================================
// Minute tick
// ============================================================================

DailyLedger::TickOutcome DailyLedger::onMinuteTick(const std::tm&  now,
                                                   const Snapshot& s,
                                                   bool            clockValid)
{
    TickOutcome out{};

    if (!clockValid) {
        // `now` is not a real date, so book nothing and leave the
        // baseline where it is. Just track what is waiting, so the
        // published record still moves.
        mPending.steps           = peekDelta(s.steps,           mLastSteps);
        mPending.floorsUp        = peekDelta(s.floorsUp,        mLastFloorsUp);
        mPending.floorsDown      = peekDelta(s.floorsDown,      mLastFloorsDown);
        mPending.activityMinutes = peekDelta(s.activityMinutes, mLastActivityMinutes);
        out.publish = composeRecord(now);
        return out;
    }

    const DailyDate today = DailyDate::fromTm(now);

    // First tick with a valid clock: settle the date the day was opened
    // under. A restored day from another date is closed and archived
    // as-is - the counts held back since boot are booked to today below.
    bool closedProvisional = false;
    if (!mDateKnown) {
        if (!mDate.sameDay(today)) {
            if (mProvisional) {
                mFinished       = mCurrent;
                mFinishedTm     = mDate.toTm();
                mFinishedRecord = HealthCalculator::compute(mFinished, mFinishedTm,
                                                            /*upToNow*/ false);
                closedProvisional = true;
            }
            startDay(today);
        }
        mDateKnown   = true;
        mProvisional = false;
        mPending     = Snapshot{};
    }

    // Cumulative - last_seen. Each delta is non-negative within one
    // boot (the source is monotonic and never resets).
    const uint32_t dSteps       = takeDelta(s.steps,           mLastSteps);
    const uint32_t dFloorsUp    = takeDelta(s.floorsUp,        mLastFloorsUp);
    const uint32_t dFloorsDown  = takeDelta(s.floorsDown,      mLastFloorsDown);
    const uint32_t dActivityMin = takeDelta(s.activityMinutes, mLastActivityMinutes);

    const bool rolled = !mDate.sameDay(today);
    out.dayRolled = rolled || closedProvisional;

    // Integrate this minute's scalar deltas. The delta covers the
    // minute that just elapsed; on a day boundary that minute occurred
    // before midnight, so it is credited to the day being closed -
    // hence the accumulate happens before the rollover snapshot.
    // Saturating adds guard the uint32 / uint16 type boundaries.
    mCurrent.steps           = addSatU32(mCurrent.steps,           dSteps);
    mCurrent.floorsUp        = addSatU16(mCurrent.floorsUp,        dFloorsUp);
    mCurrent.floorsDown      = addSatU16(mCurrent.floorsDown,      dFloorsDown);
    mCurrent.activityMinutes = addSatU16(mCurrent.activityMinutes, dActivityMin);

    if (rolled) {
        // Close the books on the finishing day (it now carries the
        // crossing-minute delta) and immediately open a fresh day.
        mFinished       = mCurrent;
        mFinishedTm     = mDate.toTm();
        mFinishedRecord = HealthCalculator::compute(mFinished, mFinishedTm,
                                                    /*upToNow*/ false);
        mCurrent = DailyMetrics{};
        mDate    = today;
        mDirty   = false;
    }

    // The HR sample is indexed by `now`, so it belongs to the current
    // (post-rollover, if any) day. Compare before writing so an HR-only
    // minute is correctly detected as a change worth persisting.
    const int h = now.tm_hour;
    const int m = now.tm_min;
    bool hrChanged = false;
    if (h >= 0 && h < 24 && m >= 0 && m < 60) {
        const uint8_t newHr = static_cast<uint8_t>(s.avgHr);
        hrChanged = (newHr != mCurrent.hrPerMinute[h][m]);
        mCurrent.hrPerMinute[h][m] = newHr;
    }

    // Dirty when the current day gained data worth a recovery flush: a
    // scalar delta booked to *this* day (so not on a rollover, where
    // the delta went to the now-archived day), or a fresh HR sample.
    const bool scalarMoved = (dSteps != 0u || dFloorsUp != 0u ||
                              dFloorsDown != 0u || dActivityMin != 0u);
    if ((!rolled && scalarMoved) || hrChanged) {
        mDirty = true;
    }

    // HR is averaged up to `now` (the real current time), not up to
    // mDate - mDate's hour/min are frozen at the day-open moment, so
    // using it would exclude every HR minute recorded after the day
    // started.
    out.publish = HealthCalculator::compute(mCurrent, now, /*upToNow*/ true);
    return out;
}

// ============================================================================
// Reset / accessors
// ============================================================================

void DailyLedger::resetTo(const DailyDate& today, const Snapshot& s,
                          bool clockValid)
{
    startDay(today);
    mDateKnown   = clockValid;
    mProvisional = false;
    mPending     = Snapshot{};

    mLastSteps           = s.steps;
    mLastFloorsUp        = s.floorsUp;
    mLastFloorsDown      = s.floorsDown;
    mLastActivityMinutes = s.activityMinutes;
}

DailyRecord DailyLedger::composeRecord(const std::tm& now) const
{
    if (mDateKnown) {
        return HealthCalculator::compute(mCurrent, now, /*upToNow*/ true);
    }

    // `now` is not a real time of day, so average HR over every slot
    // and add the counts that are waiting to be booked.
    DailyRecord r = HealthCalculator::compute(mCurrent, mDate.toTm(),
                                              /*upToNow*/ false);
    r.steps           = addSatU32(r.steps,           mPending.steps);
    r.floorsUp        = addSatU16(r.floorsUp,        mPending.floorsUp);
    r.floorsDown      = addSatU16(r.floorsDown,      mPending.floorsDown);
    r.activityMinutes = addSatU16(r.activityMinutes, mPending.activityMinutes);
    return r;
}

} // namespace DailyHealth
