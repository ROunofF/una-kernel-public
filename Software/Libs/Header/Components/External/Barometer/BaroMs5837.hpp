/**
 ******************************************************************************
 * @file    BaroMs5837.hpp
 * @brief   Barometer component (MS5837).
 *
 *          Threading: a worker thread owns the chip and its supply and does
 *          every bus access; the public API only posts commands. Construct
 *          after the scheduler starts. init()/deinit() block until the worker
 *          answers; streaming requests are asynchronous and report only
 *          whether they were queued.
 *
 *          Time: the part has no data-ready line and no counter of its own,
 *          so the worker is the clock. It measures on a grid at the applied
 *          period, and both channels of a measurement carry the system clock
 *          as read just before it was started.
 *
 *          Data: one measurement is a pressure and a temperature conversion,
 *          tens of milliseconds together, and the pressure is compensated
 *          with both. A measurement that fails delivers nothing. One that
 *          returns the ADC words of the one before is delivered and counted:
 *          a part that converts does so now and then, and only a run of
 *          them is taken for a part that has stopped.
 *
 *          Power: the part is unpowered unless a stream is wanted.
 *
 *          Recovery: while a stream is wanted, a run of failed or repeated
 *          measurements is answered with a power cycle, after a growing wait.
 *          After a bounded number of attempts the stream is stopped; the next
 *          requestStart() tries again.
 *
 *          Destruction waits for the worker without a deadline, so the bus is
 *          never left owned by a dead task.
 *
 ******************************************************************************
 */

#ifndef COMPONENT_BARO_MS5837_HPP
#define COMPONENT_BARO_MS5837_HPP

#include "IBarometer.hpp"
#include "II2c.hpp"
#include "IGpio.hpp"
#include "OS.hpp"
#include "Hardware/MS5837.hpp"
#include "Barometer/BaroCadence.hpp"
#include "Barometer/BaroRecoveryPolicy.hpp"

#include <atomic>
#include <cstdint>

class BaroMs5837 : public Interface::IBarometer
{

public:

    BaroMs5837();
    ~BaroMs5837() override;

    /**
     * @brief   Bind hardware resources. Must be called once before init().
     *          The bus is expected to be initialised already; the supply pin
     *          is taken by init().
     * @param   i2c: Bus the part sits on.
     * @param   enablePin: Pin switching the part's supply, high for on.
     * @retval  'true' on success, 'false' if the object failed to construct or
     *          config() was already called.
     */
    bool config(Interface::II2c& i2c, Interface::IGpo& enablePin);

    //// IBarometer
    bool     init()                                 override;
    void     deinit()                               override;
    bool     isInit() const                         override;
    void     setDataCallback(DataCallback* pCb)     override;

    /// Starts a stream, and is also how the rate of a wanted one is changed.
    bool     requestStart(float sampleRateHz)       override;

    bool     requestStop()                          override;
    bool     isRunning() const                      override;
    float    getMaxSampleRateHz() const             override;
    float    getMinSampleRateHz() const             override;

    /// Times the part is powered and probed before init() calls it absent.
    static constexpr uint32_t skProbeAttempts = 3;

private:

    struct Cmd {
        enum class Id : uint8_t {
            NONE = 0,
            INIT,
            DEINIT,
            START,
            STOP,
        };

        /// START only: the rate asked for, in Hz, not yet resolved to a rung.
        float sampleRateHz = 0.0f;

        Id id = Id::NONE;
    };

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
    void hStart(const Cmd& cmd);

    /// Handle a STOP: take the supply away and stop wanting the stream.
    void hStop();

    /// Run on every turn of the worker: whatever a wanted stream is owed now,
    /// a measurement or a step of its recovery.
    void service();

    /// How long the worker may wait for a command before service() has
    /// something to do.
    uint32_t waitMs() const;

    /// One measurement: read it, judge it, deliver it.
    void measure();

    /// Take the supply away and wait out the policy's wait.
    void beginRecovery(uint32_t nowMs);

    /// The power cycle a recovery was waiting to make.
    void attemptRecovery();

    /// Supply off, on, and the part's own bring-up.
    /// @retval 'true' with the part ready to measure. On 'false' the supply
    ///         is still on, and is the caller's to take away.
    bool powerUp();

    void powerDown();

    void deliver(uint64_t timestampUs, float mbar, float degC);

    // ===================== Built by the constructor =====================
    //
    // Atomics: the writing thread releases, the reading thread acquires, and a
    // thread reading what only it writes uses relaxed.

    /// Whether both queues, the mutex and the worker were created. A failure
    /// is a misconfigured build; this makes every entry point refuse rather
    /// than wait on a worker that does not exist. Written once, in the
    /// constructor, before any caller can hold the object.
    bool mUsable = false;

    /// Commands come only from callers, one for each request; the headroom
    /// is for requests made while a measurement holds the worker. Declared
    /// before the thread so it outlives it.
    OS::Queue<Cmd, 16>               mCmdQueue;

    /// One verdict per INIT or DEINIT taken off the command queue; the wake-up
    /// and the verdict travel as one item. Declared before the thread so it
    /// outlives it.
    OS::Queue<bool, 1>               mSyncReply;
    OS::Thread<BaroMs5837, 2 * 1024> mThread;

    /// Serialises config()/init()/deinit(). The request calls and the queries
    /// do NOT take it - they only post or read atomics.
    OS::Mutex mLifecycleMutex;

    // ======================= Bound once by config() ======================

    /// The part. Bound to the bus by hInit(), touched only from the worker.
    Hardware::MS5837 mDrv;

    // Bound by the first config() under mLifecycleMutex and never rebound.
    // Plain fields: the worker reads them only after init() has posted INIT
    // under the same lock and blocked on the reply, so the queue orders the
    // read after the write. An asynchronous lifecycle command would break that.
    Interface::II2c* mpI2c    = nullptr;
    Interface::IGpo* mpEnable = nullptr;

    /// Whether config() has run. Plain: only touched under mLifecycleMutex.
    bool mConfigured = false;

    // ====================== Held while the part is up ====================

    /// Whether the part was found. Stable while a command is queued, since
    /// init() blocks, so it is the one state the public request calls may
    /// gate on.
    std::atomic<bool> mIsInit { false };

    // ============================= The stream ============================

    /// Whether the part is measuring. Decisions on it are made on the worker;
    /// off the worker it lags queued commands, so it is reported by
    /// isRunning() but never gates a request.
    std::atomic<bool> mIsRunning { false };

    /// The subscriber, or nullptr. Written by a caller, read by the worker.
    /// Detaching does not wait for a call in progress; deinit() does.
    std::atomic<DataCallback*> mCallback { nullptr };

    /// When the next measurement is due. Worker-only.
    BaroCadence mCadence;

    /// The period measurements are taken at, as handed to the subscriber.
    /// Worker-only.
    uint32_t mAppliedPeriodUs = BaroCadence::skSlowestMs * 1000U;

    /// The ADC words of the last measurement read, to tell a repeat by. Kept
    /// across a power cycle: a part frozen on one value shows as a repeat on
    /// the far side of it too. Worker-only.
    Hardware::MS5837::Raw mLastRaw;
    bool                  mHaveLastRaw = false;

    // ====================== Keeping the stream alive =====================

    /// Whether a stream is meant to be up, and how its measurements have gone.
    /// Worker-only.
    BaroRecoveryPolicy::State mRecovery;

    /// Whether the part is unpowered and waiting for mRetryAtMs to be power
    /// cycled. Worker-only.
    bool     mRecovering = false;
    uint32_t mRetryAtMs  = 0;

    // ============================= Constants =============================

    /// How long the supply stays off in a power cycle, and how long the part
    /// is given after it comes back before it is spoken to.
    static constexpr uint32_t skPowerOffMs = 20;
    static constexpr uint32_t skPowerOnMs  = 10;

};

#endif // COMPONENT_BARO_MS5837_HPP
