/**
 ******************************************************************************
 * @file    MagBosch.hpp
 * @brief   Magnetometer component (BMM350).
 *
 *          Threading: a worker thread owns the chip and does every register
 *          access; the public API only posts commands. Construct after the
 *          scheduler starts. init()/deinit() block until the worker answers;
 *          streaming requests are asynchronous and report only whether they
 *          were queued.
 *
 *          Data: the field as measured, in the watch's axes, uncorrected. The
 *          part has no FIFO and a latched data-ready line, so a worker late
 *          by N periods delivers one sample where the part made N; the gap
 *          shows only in the timestamps.
 *
 *          Time: intervals come from the part's sensortime, exact at every
 *          rung. The first sample of a stream anchors it to the system clock,
 *          read in the interrupt handler, and the anchor is slewed - never
 *          stepped - as the clocks drift. Every start, recovery restart and
 *          system clock wrap takes a new anchor; within one, instants only
 *          move forward.
 *
 *          Recovery: while a stream is wanted, a missed deadline releases the
 *          latch, then rebuilds the stream at the requested rate. After a
 *          bounded number of attempts the stream is stopped and the part
 *          suspended; the next requestStart() tries again.
 *
 *          Destruction waits for the worker without a deadline, so the bus is
 *          never left owned by a dead task. A failed read costs its sample.
 *
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_BOSCH_HPP
#define COMPONENT_MAG_BOSCH_HPP

#include "IMagnetometer.hpp"
#include "II2c.hpp"
#include "IGpio.hpp"
#include "OS.hpp"
#include "Hardware/BMM350/BMM350.hpp"
#include "MagRateHandover.hpp"
#include "MagRecoveryPolicy.hpp"
#include "MagTimeBase.hpp"

#include <atomic>
#include <cstdint>

class MagBosch : public Interface::IMagnetometer,
                 public Interface::IExti::Handler
{

public:

    MagBosch();
    ~MagBosch() override;

    /**
     * @brief   Bind hardware resources. Must be called once before init().
     *          The bus is expected to be initialised already; the interrupt
     *          pin is claimed by init() and released by deinit().
     * @param   i2c: Bus the part sits on.
     * @param   intPin: Pin carrying the part's data-ready output.
     * @retval  'true' on success, 'false' if the object failed to construct or
     *          config() was already called.
     */
    bool config(Interface::II2c& i2c, Interface::IExti& intPin);

    //// IMagnetometer
    bool     init()                                 override;
    void     deinit()                               override;
    bool     isInit() const                         override;
    void     setDataCallback(DataCallback* pCb)     override;

    /// Starts a stream, and is also how the rate of a running one is changed.
    bool     requestStart(float sampleRateHz)       override;

    bool     requestStop()                          override;
    bool     isRunning() const                      override;
    float    getMaxSampleRateHz() const             override;
    float    getMinSampleRateHz() const             override;

private:

    struct Cmd {
        enum class Id : uint8_t {
            NONE = 0,
            INIT,
            DEINIT,
            START,
            STOP,
            DATA_RDY,
        };

        /// DATA_RDY only: when the interrupt fired.
        uint32_t timestampMs = 0;

        /// DATA_RDY only: the stream epoch the edge was raised in.
        uint32_t epoch = 0;

        /// START only: the rate asked for, in Hz, not yet resolved to a rung.
        float sampleRateHz = 0.0f;

        Id id = Id::NONE;
    };

    //// IExti::Handler
    void extiHandler(Interface::IExti* pExti,
                     Interface::IExti::Edge edge) override;

    void thread();

    /**
     * @brief   Post a lifecycle command and block until the worker answers it.
     * @note    Queueing fails at once rather than waiting for room. Once
     *          queued, the wait is unbounded: every queued command is
     *          answered. mLifecycleMutex keeps one caller waiting at a time.
     * @retval  'true' if the command was queued and the worker reported
     *          success, 'false' if it was not queued or the worker failed it.
     */
    bool sendSync(Cmd& cmd);
    bool sendAsync(const Cmd& cmd);

    /// Publish the verdict for the lifecycle command being handled.
    void handleSync(bool ok);

    /// Name of a command, for log messages.
    static const char* describe(Cmd::Id id);

    /// Whether a caller is parked on this command's verdict. The worker must
    /// answer such a command on every path that stops handling it.
    static bool isSync(Cmd::Id id);

    void hInit();
    void hDeinit();

    /// Put the part back where a fresh object would leave it: streaming
    /// stopped, chip suspended, interrupt pin released if it was taken.
    void releaseChip();
    void hStart(const Cmd& cmd);

    /// Take the chip out of streaming without touching mStreamWanted, so
    /// recovery can use it while still owning the stream.
    void stopStreaming();

    /// Handle a STOP: take the chip down and stop wanting the stream.
    void hStop();

    void hDataReady(const Cmd& cmd);

    /// Run on every turn of the worker: detect a wanted stream that has
    /// stalled, whatever the cause, and escalate to a full restart.
    void hWatchdog();

    /// The rate a rung runs at. Exact, so turned back into a period it
    /// resolves to the same rung.
    static float rungToRateHz(Hardware::BMM350::Odr odr);

    /// Resolve a rate to a rung, program it and record the applied period.
    /// The only place either happens, so they cannot disagree.
    bool programRate(float sampleRateHz);

    /// Push the given rate, and the interrupt configuration and power mode
    /// that go with streaming, into the chip and arm the pin.
    bool applyStreamingConfig(float sampleRateHz);

    void deliver(uint64_t timestampUs, const Hardware::BMM350::Sample& sample);

    /// Whether a sample read at readMs is the conversion its edge, seen at
    /// edgeMs, announced: read within half a period of it, and a millisecond
    /// for the tick. A later read took a newer conversion, the part having
    /// no FIFO.
    ///
    /// Both readings come off a millisecond clock whose value can hold for
    /// two of its 1024 Hz ticks, so their difference is off by up to about
    /// 2 ms. At 100 Hz and slower a stale read still shows as one. At 200 Hz
    /// that holds only for a part within about 1% of nominal, and at 400 Hz
    /// a read that took the next conversion can show as 1-2 ms and pass. The
    /// stream is then placed a period early, behind its reads, and steered
    /// back.
    static bool edgeAnnouncedRead(uint32_t edgeMs, uint32_t readMs,
                                  uint32_t periodUs);

    // ===================== Built by the constructor =====================
    //
    // Atomics: the writing thread releases, the reading thread acquires, and a
    // thread reading what only it writes uses relaxed.

    /// Whether both queues, the mutex and the worker were created. A failure
    /// is a misconfigured build; this makes every entry point refuse rather
    /// than wait on a worker that does not exist. One flag suffices because
    /// all four are statically allocated. Written once, in the constructor,
    /// before any caller can hold the object.
    bool mUsable = false;

    /// A stalled worker holds at most one DATA_RDY; the headroom is for edges
    /// raised during a drain. Declared before the thread so it outlives it.
    OS::Queue<Cmd, 16>             mCmdQueue;

    /// One verdict per INIT or DEINIT taken off the command queue; the wake-up
    /// and the verdict travel as one item. Declared before the thread so it
    /// outlives it.
    OS::Queue<bool, 1>             mSyncReply;
    OS::Thread<MagBosch, 2 * 1024> mThread;

    /// Serialises config()/init()/deinit(). The request calls and the queries
    /// do NOT take it - they only post or read atomics.
    OS::Mutex mLifecycleMutex;

    // ======================= Bound once by config() ======================

    /// The part. Bound to the bus by hInit(), touched only from the worker.
    Hardware::BMM350 mDrv;

    // Bound by the first config() under mLifecycleMutex and never rebound.
    // Plain fields: the worker reads them only after init() has posted INIT
    // under the same lock and blocked on the reply, so the queue orders the
    // read after the write. An asynchronous lifecycle command would break that.
    Interface::II2c*  mpI2c    = nullptr;
    Interface::IExti* mpIntPin = nullptr;

    /// Whether config() has run. Plain: only touched under mLifecycleMutex.
    bool mConfigured = false;

    // ====================== Held while the part is up ====================

    /// Whether the part is up. Stable while a command is queued, since init()
    /// blocks, so it is the one state the public request calls may gate on.
    std::atomic<bool> mIsInit { false };

    // ============================= The stream ============================

    /// Whether the part is delivering. Decisions on it are made on the
    /// worker; off the worker it lags queued commands, so it is reported by
    /// isRunning() but never gates a request.
    std::atomic<bool> mIsRunning { false };

    /// Moved on by the worker once the part converts for a new stream. The
    /// interrupt handler copies it into DATA_RDY, so an edge raised before -
    /// during the part's start-up wait, or queued behind a stop - is told
    /// apart from one of the stream.
    std::atomic<uint32_t> mEdgeEpoch { 0 };

    /// The subscriber, or nullptr. Written by a caller, read by the worker.
    /// Detaching does not wait for a call in progress; deinit() does.
    std::atomic<DataCallback*> mCallback { nullptr };

    // ======================== The time base =============================

    /// Instants for the samples. Touched only on the worker.
    MagTimeBase mTimeBase;

    /// Whether the stream has delivered its first sample. That one places
    /// the stream in time, so it must be a sample its own edge announced.
    /// Worker-only.
    bool mStreamPlaced = false;

    /// The part's count for the last sample delivered. The same count again
    /// is the same conversion read twice. Worker-only.
    uint32_t mLastSensorTime = 0;

    /// The rate asked for, and the period the chip was actually given.
    /// Recovery rebuilds from the rate: the applied period may be left over
    /// from an earlier stream. The period is in microseconds because the slow
    /// rungs are exact there. Worker-only: the rate is written where START is
    /// handled, the period by programRate() once the chip has taken it.
    float    mWantedRateHz    = skDefaultSampleRateHz;
    uint32_t mAppliedPeriodUs =
            static_cast<uint32_t>(1000000.0f / skDefaultSampleRateHz);

    /// Which of the two rates a sample was converted at, after the rate was
    /// changed on a running stream. Worker-only.
    MagRateHandover mRateHandover;

    // ====================== Keeping the stream alive =====================

    /// Whether a stream is meant to be up, as opposed to mIsRunning. Set where
    /// START is handled, before the attempt, so a failed start is recovered;
    /// cleared by STOP, by deinit, and by recovery giving up. Worker-only.
    bool mStreamWanted = false;

    /// When the last sample was read - not delivered: a stream with no
    /// subscriber is still alive - and the recovery actions spent since. Both
    /// reset by every sample. Worker-only.
    uint32_t mLastSampleMs     = 0;
    uint32_t mRecoveryAttempts = 0;

    // ============================= Constants =============================

    /// The Earth field seen through a wrist is a low-bandwidth signal.
    static constexpr float skDefaultSampleRateHz = 25.0f;

    /// Requested averaging. Reduced automatically at the ODRs that forbid it.
    static constexpr Hardware::BMM350::Averaging skAveraging =
            Hardware::BMM350::Averaging::AVG_4;

    /// EXTI priority. At configMAX_SYSCALL_INTERRUPT_PRIORITY, the lowest
    /// number still allowed to post from a handler.
    static constexpr uint32_t skIrqPriority = 5;

    /// Upper bound on the latched-pin drain loop so it cannot spin forever.
    static constexpr uint32_t skPinDrainGuard = 8;


};

#endif // COMPONENT_MAG_BOSCH_HPP
