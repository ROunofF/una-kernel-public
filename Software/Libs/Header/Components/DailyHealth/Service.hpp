/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   Daily Health Service - orchestrator of the Daily Health
 *          subsystem.
 *
 *          Owns nothing heavyweight directly. Collaborates with:
 *           - IClock          - wall-clock time
 *           - IActivityDataSource - cumulative-from-boot counters
 *                               for per-minute delta accumulation
 *           - IFeatureSource  - features the activity FSM needs (TBD)
 *           - ISensorControl  - sensor (de)activation for FSM (TBD)
 *           - IDailyStorage   - persistence (FS / JSON / CRC)
 *
 *          Threading model:
 *           - One internal worker task. All non-trivial work runs there.
 *           - Public API calls are sync to the caller; the implementation
 *             marshals onto the worker as needed.
 *           - getCurrentRecord() is RAM-only and runs under a short
 *             mutex from any caller's stack - no marshalling.
 *           - getStoredRecord() and notifyMsdMode() block the caller
 *             until the worker has processed the request. Marshalling
 *             uses an RPC slot owned by the Service itself, serialised
 *             by mRpcMutex, so caller stack lifetime is irrelevant.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_SERVICE_HPP
#define COMPONENT_DAILY_HEALTH_SERVICE_HPP

#include <cstdint>
#include <ctime>

#include "OS.hpp"
#include "Observer.hpp"

#include "DailyHealth/Types.hpp"
#include "DailyHealth/IClock.hpp"
#include "DailyHealth/IDailyStorage.hpp"
#include "DailyHealth/DailyLedger.hpp"
#include "Activity/IActivityDataSource.hpp"

namespace DailyHealth
{

class Service
{
public:
    class Callback
    {
    public:
        virtual void onDailyValuesChange(DailyRecord record) { (void)record; }
    protected:
        virtual ~Callback() = default;
    };

    /**
     * @param flushPeriodMs Period for automatic in-progress saves.
     *                      Defaults to 10 minutes. Critical events
     *                      (day rollover, MSD entry, factory reset,
     *                      shutdown) flush regardless of this period.
     */
    Service(IClock&                        clock,
            Activity::IActivityDataSource& source,
            IDailyStorage&                 storage,
            uint32_t                       flushPeriodMs = 10u * 60u * 1000u);

    ~Service();

    bool init();
    bool deinit();
    bool isInitialized() const;

    void attachCallback(Callback* cb);
    void detachCallback(Callback* cb);

    /**
     * @brief Read the current-day record, computed on the fly from the
     *        in-RAM metrics. No FS access. Safe from any task.
     */
    DailyRecord getCurrentRecord();

    /**
     * @brief Read a stored record for a past date. Blocks the caller
     *        until the worker has loaded it from storage.
     */
    DailyRecord getStoredRecord(std::tm date);

    /**
     * @brief Read the current-day per-minute HR grid (RAM-only, safe
     *        from any task). Result is a 24x60 matrix where 0 means
     *        "no valid sample for this minute".
     */
    void getCurrentHrSeries(DailyHrMatrix& out);

    /**
     * @brief Read a stored per-minute HR grid for a past date. Blocks
     *        the caller until the worker has loaded it from storage.
     *        On miss / corrupt the matrix is zeroed.
     */
    void getStoredHrSeries(std::tm date, DailyHrMatrix& out);

    /**
     * @brief Same as @ref getStoredHrSeries but returns only one hour
     *        (60 bytes) chosen by @p date.tm_hour. Lets the BLE layer
     *        keep a tiny stack-side buffer instead of a 1.4 KB matrix
     *        when streaming a day in hourly chunks. Cache behaviour is
     *        identical: today / cache-hit run on the caller's stack;
     *        a miss takes one RPC to fill mHrCache, then copies just
     *        the requested hour.
     */
    void getStoredHrSeries(std::tm date, uint8_t (&out)[60]);

    /**
     * @brief Toggle USB Mass-Storage exposure. Blocks the caller until
     *        any pending in-progress metrics have been flushed and the
     *        storage layer has been put into the new mode - caller can
     *        rely on "no further FS activity from us" by the time the
     *        call returns with @p active = true.
     */
    void notifyMsdMode(bool active);

    /**
     * @brief Persist the in-progress per-day metrics now, blocking the
     *        caller until the worker has done so.
     *
     * Intended for an orderly shutdown: call it before powering the
     * watch off so the current day's progress survives. A no-op on the
     * worker side if nothing is dirty, and a no-op (logged) if the
     * subsystem is not initialised.
     */
    void flush();

    /**
     * @brief Factory reset: clears the in-RAM metrics and erases all
     *        persisted state. Blocks the caller until the worker has
     *        finished, so a clean slate is guaranteed on return. A no-op
     *        (logged) if the subsystem is not initialised.
     */
    void resetAll();

private:
    IClock&                        mClock;
    Activity::IActivityDataSource& mSource;
    IDailyStorage&                 mStorage;

    const uint32_t mFlushPeriodMs;

    // Slots: Backend (1) + 4 sink sensors (HRMetrics, DailySteps,
    // DailyFloors, DailyActivityTime). Headroom for one more future
    // consumer keeps us from re-resizing on next iteration.
    Observer::Holder<Callback, 8> mObserver{};

    /// Mutex protecting @ref mLedger and the HR cache trio
    /// (mHrCache / mHrCacheDate / mHrCacheValid). Mutable so const
    /// accessors (composeRecordLocked) can take it.
    mutable OS::Mutex mDataMutex{};

    /// Running day record + delta-accumulation / rollover / restore
    /// logic. Pure - no OS, no I/O (see DailyLedger). This Service
    /// wraps it with the worker thread, timers, persistence and
    /// observer fan-out, and serialises access under mDataMutex.
    DailyLedger mLedger{};

    /// Last-loaded archived HR matrix. Only the worker writes the
    /// payload; readers see it via mHrCacheValid (set under mDataMutex
    /// after the worker has finished writing - the implicit barrier on
    /// mutex release/acquire publishes mHrCache.cells safely).
    /// BLE typically reads one day in 24 hour-sized chunks; this cache
    /// turns 24 file loads into 1 + 23 cache hits.
    DailyHrMatrix mHrCache{};
    DailyDate     mHrCacheDate{};
    bool          mHrCacheValid{false};

    /// Worker-task-only scratch buffers. DailyMetrics (~1.5 KB) and
    /// DailyHrMatrix (1.4 KB) are too big to live on the 4 KB worker
    /// stack alongside an FatFS f_stat() frame. mScratchMetrics is the
    /// load buffer for storage.loadCurrent() at boot and the snapshot
    /// source for handleFlush(); mScratchHr stages the HR matrix for
    /// storage.saveRecord() on boot-archive and day rollover. Throwaway,
    /// NOT a cache: the worker is the only writer/reader.
    DailyMetrics  mScratchMetrics{};
    DailyHrMatrix mScratchHr{};

    // ----- Async commands ----------------------------------------------
    struct Cmd
    {
        enum class Id : uint8_t
        {
            NONE = 0,
            MINUTE_TICK,
            FLUSH_TICK,
            RPC,           ///< Drives the RPC slot below.
        };
        Id id;
    };

    OS::Queue<Cmd, 6>           mCmdQueue{};
    OS::Thread<Service, 4*1024> mThread{};

    OS::Timer<Service> mMinuteTimer{};
    OS::Timer<Service> mFlushTimer{};

    // ----- Blocking RPC slot --------------------------------------------
    //
    // Pattern: the caller takes mRpcMutex (which serialises all blocking
    // calls), writes the request into mRpc, posts a single Cmd::RPC,
    // then waits on mRpcDone. The worker reads mRpc, runs the matching
    // handler, fills the result back into mRpc, and signals mRpcDone.
    //
    // Reply storage lives inside Service - never on the caller's stack.
    // That makes it safe even if a worst-case scheduler lets the worker
    // run long after a hypothetical timeout (we do not use a timeout
    // here - the worker is bounded; pulling the rug under the worker
    // mid-RPC would only happen during deinit, which the caller drives).
    enum class RpcKind : uint8_t
    {
        MSD,
        GET_STORED,
        GET_STORED_HR,
        FLUSH,         ///< Synchronous persist of the in-progress day.
        RESET_ALL,     ///< Synchronous factory reset.
    };

    struct Rpc
    {
        RpcKind kind;
        union
        {
            struct { bool active; }                       msd;
            struct { std::tm date; DailyRecord result; }  getStored;
            // GET_STORED_HR has no result field: the worker writes the
            // matrix directly into mHrCache and the caller copies from
            // there once the RPC unblocks.
            struct { std::tm date; }                      getStoredHr;
        } payload;
    };

    OS::Mutex      mRpcMutex{};
    OS::EventFlags mRpcDone{};
    Rpc            mRpc{};

    // ----- Implementation ----------------------------------------------
    void thread();
    void minuteTimerCallback(void* arg);
    void flushTimerCallback(void* arg);

    void bootRestore();
    void handleMinuteTick();
    void handleFlush();
    void handleResetAll();
    void handleRpc();
    void handleMsdToggle(bool active);
    void handleGetStored(const std::tm& date, DailyRecord& outResult);
    void handleGetStoredHr(const std::tm& date);

    DailyRecord composeRecordLocked() const;

    /// Arm @ref mMinuteTimer to fire at the next minute boundary
    /// according to the clock.
    void armMinuteTimer();

    /// Push an empty-payload command onto the worker queue. Returns
    /// false if the queue is full for ~100 ms (treated as a soft
    /// failure by callers).
    bool postCmd(Cmd::Id id);
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_SERVICE_HPP
