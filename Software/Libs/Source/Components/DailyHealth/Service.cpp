/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   Daily Health Service implementation.
 *
 *          See Service.hpp for the threading model and the RPC-slot
 *          rationale.
 ******************************************************************************
 */

#include "DailyHealth/Service.hpp"

#define LOG_MODULE_PRX      "DH.Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cstring>

#include "ScopeGuard.hpp"

namespace DailyHealth
{

namespace {

constexpr uint32_t kRpcDoneFlag = (1u << 0);

} // namespace

// ============================================================================
// Construction
// ============================================================================

Service::Service(IClock&                        clock,
                 Activity::IActivityDataSource& source,
                 IDailyStorage&                 storage,
                 uint32_t                       flushPeriodMs)
    : mClock(clock)
    , mSource(source)
    , mStorage(storage)
    , mFlushPeriodMs(flushPeriodMs)
{
    mDataMutex.init(LOG_MODULE_PRX);
    mRpcMutex.init(LOG_MODULE_PRX);
    mRpcDone.init(LOG_MODULE_PRX);
    mCmdQueue.init(LOG_MODULE_PRX);

    mMinuteTimer.init(LOG_MODULE_PRX, this, &Service::minuteTimerCallback,
                      /*periodic*/ false, this);
    mFlushTimer.init(LOG_MODULE_PRX, this, &Service::flushTimerCallback,
                     /*periodic*/ true, this);
}

Service::~Service()
{
    deinit();
}

bool Service::init()
{
    return mThread.init("DHService", this, &Service::thread,
                        osPriorityNormal, 5000);
}

bool Service::deinit()
{
    if (!mThread.isInitialized()) {
        return true;
    }

    mThread.deinit();

    // Wake any RPC caller still waiting - the worker is gone and will
    // not signal them itself.
    mRpcDone.set(kRpcDoneFlag);
    return true;
}

bool Service::isInitialized() const
{
    return mThread.isInitialized();
}

void Service::attachCallback(Callback* cb)
{
    mObserver.add(cb);
}

void Service::detachCallback(Callback* cb)
{
    mObserver.del(cb);
}

// ============================================================================
// Public API
// ============================================================================

DailyRecord Service::getCurrentRecord()
{
    OS::MutexCS cs(mDataMutex);
    return composeRecordLocked();
}

DailyRecord Service::getStoredRecord(std::tm date)
{
    if (!isInitialized()) {
        LOG_WARNING("getStoredRecord: DH not initialized\n");
        return DailyRecord{};
    }

    // Today goes straight from RAM on the caller's stack - same fast
    // path that getStoredHrSeries uses, and equivalent to a direct
    // getCurrentRecord() call. Saves an RPC round-trip when the BLE
    // app polls "today" via the storage-style getter.
    const DailyDate wantDate = DailyDate::fromTm(date);
    {
        OS::MutexCS cs(mDataMutex);
        if (mLedger.date().sameDay(wantDate)) {
            return composeRecordLocked();
        }
    }

    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind = RpcKind::GET_STORED;
    mRpc.payload.getStored.date   = date;
    mRpc.payload.getStored.result = DailyRecord{};

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("getStoredRecord: queue push failed\n");
        return DailyRecord{};
    }

    // Wait until the worker fills the result. No timeout: the worker is
    // bounded, and the RPC mutex serialises callers. deinit() signals
    // the flag if the worker is being torn down.
    mRpcDone.wait(kRpcDoneFlag);

    return mRpc.payload.getStored.result;
}

void Service::getCurrentHrSeries(DailyHrMatrix& out)
{
    OS::MutexCS cs(mDataMutex);
    std::memcpy(out.cells, mLedger.metrics().hrPerMinute, sizeof(out.cells));
}

void Service::getStoredHrSeries(std::tm date, DailyHrMatrix& out)
{
    if (!isInitialized()) {
        LOG_WARNING("getStoredHrSeries: DH not initialized\n");
        out = DailyHrMatrix{};
        return;
    }

    const DailyDate wantDate = DailyDate::fromTm(date);

    // Fast paths on the caller's stack: today goes straight from RAM,
    // and a cached past day is a single 1.4 KB memcpy. Both avoid the
    // RPC trip entirely - critical when BLE reads a day in 24 hourly
    // chunks for one calendar date.
    {
        OS::MutexCS cs(mDataMutex);
        if (mLedger.date().sameDay(wantDate)) {
            std::memcpy(out.cells, mLedger.metrics().hrPerMinute,
                        sizeof(out.cells));
            return;
        }
        if (mHrCacheValid && mHrCacheDate.sameDay(wantDate)) {
            std::memcpy(out.cells, mHrCache.cells, sizeof(out.cells));
            return;
        }
    }

    // Cache miss for a past day - go to the worker for the file load.
    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind                     = RpcKind::GET_STORED_HR;
    mRpc.payload.getStoredHr.date = date;

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("getStoredHrSeries: queue push failed\n");
        out = DailyHrMatrix{};
        return;
    }

    mRpcDone.wait(kRpcDoneFlag);

    // Worker wrote into mHrCache (or marked it invalid on FS error).
    OS::MutexCS cs(mDataMutex);
    if (mHrCacheValid && mHrCacheDate.sameDay(wantDate)) {
        std::memcpy(out.cells, mHrCache.cells, sizeof(out.cells));
    } else {
        out = DailyHrMatrix{};
    }
}

void Service::getStoredHrSeries(std::tm date, uint8_t (&out)[60])
{
    if (!isInitialized()) {
        LOG_WARNING("getStoredHrSeries: DH not initialized\n");
        std::memset(out, 0, sizeof(out));
        return;
    }

    const int hour = date.tm_hour;
    if (hour < 0 || hour >= 24) {
        std::memset(out, 0, sizeof(out));
        return;
    }

    const DailyDate wantDate = DailyDate::fromTm(date);

    // Same cache path as the matrix overload, but only the requested
    // hour-row gets copied out. Lets BLE stream a day in 24 60-byte
    // chunks without keeping a 1.4 KB matrix on its own stack.
    {
        OS::MutexCS cs(mDataMutex);
        if (mLedger.date().sameDay(wantDate)) {
            std::memcpy(out, mLedger.metrics().hrPerMinute[hour], sizeof(out));
            return;
        }
        if (mHrCacheValid && mHrCacheDate.sameDay(wantDate)) {
            std::memcpy(out, mHrCache.cells[hour], sizeof(out));
            return;
        }
    }

    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind                     = RpcKind::GET_STORED_HR;
    mRpc.payload.getStoredHr.date = date;

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("getStoredHrSeries: queue push failed\n");
        std::memset(out, 0, sizeof(out));
        return;
    }

    mRpcDone.wait(kRpcDoneFlag);

    OS::MutexCS cs(mDataMutex);
    if (mHrCacheValid && mHrCacheDate.sameDay(wantDate)) {
        std::memcpy(out, mHrCache.cells[hour], sizeof(out));
    } else {
        std::memset(out, 0, sizeof(out));
    }
}

void Service::notifyMsdMode(bool active)
{
    if (!isInitialized()) {
        LOG_WARNING("notifyMsdMode: DH not initialized\n");
        return;
    }

    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind = RpcKind::MSD;
    mRpc.payload.msd.active = active;

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("notifyMsdMode: queue push failed\n");
        return;
    }

    // Block until storage is in the requested state.
    mRpcDone.wait(kRpcDoneFlag);
}

void Service::flush()
{
    if (!isInitialized()) {
        LOG_WARNING("flush: DH not initialized\n");
        return;
    }

    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind = RpcKind::FLUSH;

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("flush: queue push failed\n");
        return;
    }

    // Block until the worker has persisted the in-progress metrics.
    mRpcDone.wait(kRpcDoneFlag);
}

void Service::resetAll()
{
    if (!isInitialized()) {
        LOG_WARNING("resetAll: DH not initialized\n");
        return;
    }

    OS::MutexCS rpcLock(mRpcMutex);

    mRpc.kind = RpcKind::RESET_ALL;

    mRpcDone.clear(kRpcDoneFlag);

    if (!postCmd(Cmd::Id::RPC)) {
        LOG_ERROR("resetAll: queue push failed\n");
        return;
    }

    // Block until the worker has erased all state and re-anchored the
    // delta baseline - callers rely on a clean slate on return.
    mRpcDone.wait(kRpcDoneFlag);
}

// ============================================================================
// Worker task
// ============================================================================

void Service::thread()
{
    bootRestore();
    mThread.setInitialized();

    OS::CancellableGuard guard(&mThread, &mCmdQueue);

    armMinuteTimer();
    mFlushTimer.start(mFlushPeriodMs);

    // Fire the first tick immediately instead of waiting up to a full
    // minute for the timer boundary. It credits the boot-to-now delta
    // and, crucially, notifies subscribers so the daily sink sensors
    // publish today's restored values straight away rather than sitting
    // empty until the first periodic tick.
    postCmd(Cmd::Id::MINUTE_TICK);

    LOG_INFO("Initialized\n");

    while (!mThread.shouldStop()) {
        Cmd cmd{};
        mCmdQueue.pop(cmd);

        if (mThread.shouldStop()) {
            break;
        }

        switch (cmd.id) {
            case Cmd::Id::MINUTE_TICK:  handleMinuteTick(); break;
            case Cmd::Id::FLUSH_TICK:   handleFlush();      break;
            case Cmd::Id::RPC:          handleRpc();        break;
            case Cmd::Id::NONE:         break;
        }
    }

    // Best-effort save before tearing down. The data source has
    // its own lifecycle (owned by Activity::Service), nothing for
    // us to deinit on it.
    handleFlush();
    LOG_INFO("Deinitialized\n");
}

void Service::minuteTimerCallback(void* /*arg*/)
{
    // Re-arm to the next minute boundary, then post the tick.
    armMinuteTimer();
    postCmd(Cmd::Id::MINUTE_TICK);
}

void Service::flushTimerCallback(void* /*arg*/)
{
    // Periodic timer - no re-arm needed.
    postCmd(Cmd::Id::FLUSH_TICK);
}

void Service::armMinuteTimer()
{
    const std::tm now = mClock.now();
    const uint32_t toNextMinMs =
        (60u - static_cast<uint32_t>(now.tm_sec)) * 1000u;
    mMinuteTimer.start(toNextMinMs);
}

bool Service::postCmd(Cmd::Id id)
{
    Cmd cmd{};
    cmd.id = id;
    return mCmdQueue.push(cmd, 100);
}

// ============================================================================
// Boot restore
// ============================================================================

void Service::bootRestore()
{
    // Validity first: the clock is set before it is flagged valid, so a
    // valid flag guarantees the time read after it is real.
    const bool      clockValid = mClock.isValid();
    const DailyDate today      = DailyDate::fromTm(mClock.now());

    // Load into the scratch member, not a stack local: DailyMetrics is
    // ~1.5 KB and FatFS' f_stat() inside loadCurrent() pulls another
    // few hundred bytes of frame; together they overflowed the 4 KB
    // worker stack and HardFaulted on first boot.
    mScratchMetrics = DailyMetrics{};
    std::tm restoredTm{};
    const bool hadRecovery = mStorage.loadCurrent(mScratchMetrics, restoredTm);

    DailyLedger::RestoreOutcome ro{};
    {
        OS::MutexCS cs(mDataMutex);
        ro = mLedger.bootRestore(today, hadRecovery,
                                 mScratchMetrics, restoredTm, clockValid);
    }

    if (ro.archiveStale) {
        // Recovery belonged to an earlier day - finalise it as a JSON
        // archive entry. finishedMetrics() is worker-private; reading
        // it outside the lock is safe.
        std::memcpy(mScratchHr.cells, mLedger.finishedMetrics().hrPerMinute,
                    sizeof(mScratchHr.cells));
        mStorage.saveRecord(mLedger.finishedTm(), mLedger.finishedRecord(),
                            mScratchHr);
        mStorage.eraseCurrent();
        LOG_INFO("Finalised stale recovery as archive\n");
    } else if (!mLedger.dateKnown()) {
        LOG_INFO("Clock not set - holding the day until it is\n");
    } else if (hadRecovery) {
        LOG_DEBUG("Recovered metrics for today\n");
    }

    // No echoToProvider step: sensors are monotonic from boot,
    // there is nothing for us to write back. UI / BLE consumers
    // that want today's view subscribe to the DAILY_* sensors,
    // which receive their values via the DH::Callback channel.
}

// ============================================================================
// Minute tick / flush
// ============================================================================

void Service::handleMinuteTick()
{
    // Validity before time, as in bootRestore().
    const bool    clockValid = mClock.isValid();
    const std::tm now        = mClock.now();

    // Sample cumulative-from-boot readings from Activity. Each is
    // monotonic within one boot (Activity never resets); the ledger
    // turns them into non-negative per-minute deltas.
    DailyLedger::Snapshot snap{};
    snap.steps           = mSource.stepsSinceBoot();
    mSource.floorsSinceBoot(snap.floorsUp, snap.floorsDown);
    snap.activityMinutes = mSource.activityMinutesSinceBoot();
    snap.avgHr           = mSource.currentAvgHr();

    DailyLedger::TickOutcome out{};
    {
        OS::MutexCS cs(mDataMutex);
        out = mLedger.onMinuteTick(now, snap, clockValid);
    }

    // The HR-metrics sensor (Sensor::HeartRateMetricsDaily) and the daily
    // sink sensors receive AHR / RHR / steps / floors through this
    // same DH::Callback channel as the Backend.
    LOG_DEBUG("Steps: %u, Floors Up/Down: %u/%u, Activity %u, AHR: %u, RHR %u\n",
            out.publish.steps, out.publish.floorsUp, out.publish.floorsDown,
            out.publish.activityMinutes,
             out.publish.averageHeartRate, out.publish.restingHeartRate);
    mObserver.notify(&Callback::onDailyValuesChange, out.publish);

    if (out.dayRolled) {
        // The ledger has already closed the finishing day (crossing-
        // minute delta included) and opened a fresh one. We only need
        // to persist the day it handed back. finishedMetrics() is
        // worker-private, so reading it outside the lock is safe.
        std::memcpy(mScratchHr.cells, mLedger.finishedMetrics().hrPerMinute,
                    sizeof(mScratchHr.cells));
        mStorage.saveRecord(mLedger.finishedTm(), mLedger.finishedRecord(),
                            mScratchHr);
        mStorage.eraseCurrent();

        LOG_INFO("Day rollover - archived previous day\n");
    }
}

void Service::handleFlush()
{
    DailyDate snapDate{};

    {
        OS::MutexCS cs(mDataMutex);
        if (!mLedger.dirty()) {
            return;
        }
        // mScratchMetrics holds the snapshot we are about to persist.
        // Safe because the worker is single-threaded and these handlers
        // are never reentrant.
        mScratchMetrics = mLedger.metrics();
        snapDate        = mLedger.date();
    }

    if (!mStorage.saveCurrent(snapDate.toTm(), mScratchMetrics)) {
        LOG_WARNING("Recovery save failed (read-only or FS error)\n");
        return;
    }

    {
        OS::MutexCS cs(mDataMutex);
        // Only clear if we still own the data we wrote - otherwise the
        // next minute tick advanced the ledger after our snapshot and
        // we must keep the dirty flag set.
        const DailyMetrics& cur = mLedger.metrics();
        if (cur.steps           == mScratchMetrics.steps           &&
            cur.floorsUp        == mScratchMetrics.floorsUp        &&
            cur.floorsDown      == mScratchMetrics.floorsDown      &&
            cur.activityMinutes == mScratchMetrics.activityMinutes &&
            std::memcmp(cur.hrPerMinute, mScratchMetrics.hrPerMinute,
                        sizeof(mScratchMetrics.hrPerMinute)) == 0)
        {
            mLedger.clearDirty();
        }
    }
}

void Service::handleResetAll()
{
    LOG_INFO("Reset all\n");

    mStorage.eraseAll();

    const bool      clockValid = mClock.isValid();
    const DailyDate today      = DailyDate::fromTm(mClock.now());

    // Re-anchor the delta-acc baseline to whatever the source is
    // reporting right now. The source is monotonic and never reset,
    // so we cannot "zero" it; instead the ledger forgets the historical
    // last_seen so the next minute tick credits zero (current -
    // current = 0) and the fresh day starts from a clean slate.
    DailyLedger::Snapshot snap{};
    snap.steps           = mSource.stepsSinceBoot();
    mSource.floorsSinceBoot(snap.floorsUp, snap.floorsDown);
    snap.activityMinutes = mSource.activityMinutesSinceBoot();
    snap.avgHr           = 0.0f;

    {
        OS::MutexCS cs(mDataMutex);
        mLedger.resetTo(today, snap, clockValid);
        mHrCacheValid = false;
    }

    DailyRecord empty{};
    mObserver.notify(&Callback::onDailyValuesChange, empty);
}

// ============================================================================
// RPC slot dispatch
// ============================================================================

void Service::handleRpc()
{
    switch (mRpc.kind) {
        case RpcKind::MSD:
            handleMsdToggle(mRpc.payload.msd.active);
            break;
        case RpcKind::GET_STORED:
            handleGetStored(mRpc.payload.getStored.date,
                            mRpc.payload.getStored.result);
            break;
        case RpcKind::GET_STORED_HR:
            handleGetStoredHr(mRpc.payload.getStoredHr.date);
            break;
        case RpcKind::FLUSH:
            handleFlush();
            break;
        case RpcKind::RESET_ALL:
            handleResetAll();
            break;
    }

    // Wake the caller. The result (if any) is already in mRpc.
    mRpcDone.set(kRpcDoneFlag);
}

void Service::handleMsdToggle(bool active)
{
    if (active) {
        // Force on-disk durability before storage goes read-only.
        handleFlush();
        mStorage.setReadOnly(true);
        LOG_INFO("MSD on - storage read-only\n");
    } else {
        // The user may have edited / replaced archive files via MSD
        // while we were read-only; the cached HR matrix is no longer
        // trustworthy for any past date.
        {
            OS::MutexCS cs(mDataMutex);
            mHrCacheValid = false;
        }
        mStorage.setReadOnly(false);
        LOG_INFO("MSD off - storage writable\n");
    }
}

void Service::handleGetStored(const std::tm& date, DailyRecord& outResult)
{
    const DailyDate wantDate = DailyDate::fromTm(date);

    // Match against the ledger's day, not the wall clock: while the
    // clock is unset it reads 2000-01-01 but the ledger holds the
    // restored day.
    {
        OS::MutexCS cs(mDataMutex);
        if (mLedger.date().sameDay(wantDate)) {
            outResult = composeRecordLocked();
            return;
        }
    }

    // Returns zero-filled record on miss.
    mStorage.loadRecord(date, outResult);
}

void Service::handleGetStoredHr(const std::tm& date)
{
    const DailyDate wantDate = DailyDate::fromTm(date);

    // Another caller may have already populated the cache for this
    // date while we were queued behind mRpcMutex - skip the file load.
    {
        OS::MutexCS cs(mDataMutex);
        if (mHrCacheValid && mHrCacheDate.sameDay(wantDate)) {
            return;
        }
        // Mark the cache invalid before we start writing into it -
        // any concurrent reader on the BLE / UI stack will skip the
        // cache and queue behind us on mRpcMutex.
        mHrCacheValid = false;
    }

    const bool ok = mStorage.loadHrSeries(date, mHrCache);

    // Commit. Mutex acquire/release publishes the writes we just made
    // to mHrCache.cells to subsequent readers.
    OS::MutexCS cs(mDataMutex);
    if (ok) {
        mHrCacheDate  = wantDate;
        mHrCacheValid = true;
    }
    // else: mHrCacheValid stays false; getStoredHrSeries returns zeros.
}

DailyRecord Service::composeRecordLocked() const
{
    return mLedger.composeRecord(mClock.now());
}

} // namespace DailyHealth
