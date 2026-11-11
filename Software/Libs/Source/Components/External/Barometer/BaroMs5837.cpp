/**
 ******************************************************************************
 * @file    BaroMs5837.cpp
 * @brief   Barometer component (MS5837).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Barometer/BaroMs5837.hpp"

#define LOG_MODULE_PRX      "BaroMs5837"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "System.hpp"

BaroMs5837::BaroMs5837()
{
    const bool queueOk = mCmdQueue.init(LOG_MODULE_PRX);
    if (!queueOk) {
        LOG_ERROR("Command queue could not be created\n");
    }

    const bool replyOk = mSyncReply.init(LOG_MODULE_PRX);
    if (!replyOk) {
        LOG_ERROR("Reply queue could not be created\n");
    }

    const bool mutexOk = mLifecycleMutex.init(LOG_MODULE_PRX);
    if (!mutexOk) {
        LOG_ERROR("Lifecycle mutex could not be created\n");
    }

    // Registered before the thread exists, so mThread.requestStop() can
    // unblock a parked pop() for every instant the worker is alive.
    mThread.registerCancellable(&mCmdQueue);

    // Only started if the queue exists: pop() on a null queue does not block,
    // so the worker would spin.
    bool threadOk = false;
    if (queueOk) {
        threadOk = mThread.init(LOG_MODULE_PRX, this, &BaroMs5837::thread,
                osPriorityNormal);
        if (!threadOk) {
            LOG_ERROR("Worker thread failed to start\n");
        }
    }

    mUsable = queueOk && replyOk && mutexOk && threadOk;
}

BaroMs5837::~BaroMs5837()
{
    // The command queue is a registered cancellable, so this also unblocks
    // the pop() the worker is parked on.
    mThread.requestStop();

    mThread.join();

    // The worker has parked, and on its way out took the supply away.
    mThread.deinit();
    mCmdQueue.deinit();
    mSyncReply.deinit();
    mLifecycleMutex.deinit();
}

bool BaroMs5837::config(Interface::II2c& i2c, Interface::IGpo& enablePin)
{
    // Checked before the lock: a mutex that failed to construct fails acquire
    // silently, and MutexCS does not look at the result.
    if (!mUsable) {
        LOG_ERROR("config() on an object that failed to construct\n");
        return false;
    }

    // The check and the write are one step under the lifecycle lock, which
    // also orders init()'s plain read of mConfigured.
    OS::MutexCS lock(mLifecycleMutex);

    if (mConfigured) {
        LOG_ERROR("config() called twice\n");
        return false;
    }

    mpI2c       = &i2c;
    mpEnable    = &enablePin;
    mConfigured = true;

    return true;
}

bool BaroMs5837::init()
{
    // Before the lock, as in config().
    if (!mUsable) {
        LOG_ERROR("init() on an object that failed to construct\n");
        return false;
    }

    OS::MutexCS lock(mLifecycleMutex);

    if (!mConfigured) {
        LOG_ERROR("init() before config()\n");
        return false;
    }

    if (mIsInit.load(std::memory_order_acquire)) {
        return true;
    }

    Cmd cmd {};
    cmd.id = Cmd::Id::INIT;

    return sendSync(cmd);
}

void BaroMs5837::deinit()
{
    if (!mUsable) {
        return;
    }

    OS::MutexCS lock(mLifecycleMutex);

    if (!mIsInit.load(std::memory_order_acquire)) {
        return;
    }

    Cmd cmd {};
    cmd.id = Cmd::Id::DEINIT;

    // Must return before the object is destroyed. A stop arriving while this
    // is in flight answers it as a failure rather than leaving it parked.
    (void) sendSync(cmd);
}

bool BaroMs5837::isInit() const
{
    return mIsInit.load(std::memory_order_acquire);
}

void BaroMs5837::setDataCallback(DataCallback* pCb)
{
    mCallback.store(pCb, std::memory_order_release);
}

bool BaroMs5837::requestStart(float sampleRateHz)
{
    // Refused here, where the caller can act on it. hStart() checks again for
    // a deinit that lands after this.
    if (!mIsInit.load(std::memory_order_acquire)) {
        LOG_WARNING("START before init\n");
        return false;
    }

    // The rate travels as asked; the worker resolves it to a rung.
    Cmd cmd {};
    cmd.id           = Cmd::Id::START;
    cmd.sampleRateHz = sampleRateHz;

    return sendAsync(cmd);
}

bool BaroMs5837::requestStop()
{
    // Same gate as requestStart(): an uninitialised part was never running.
    if (!mIsInit.load(std::memory_order_acquire)) {
        LOG_WARNING("STOP before init\n");
        return false;
    }

    Cmd cmd {};
    cmd.id = Cmd::Id::STOP;

    return sendAsync(cmd);
}

bool BaroMs5837::isRunning() const
{
    return mIsRunning.load(std::memory_order_acquire);
}

float BaroMs5837::getMaxSampleRateHz() const
{
    return BaroCadence::maxRateHz();
}

float BaroMs5837::getMinSampleRateHz() const
{
    return BaroCadence::minRateHz();
}

const char* BaroMs5837::describe(Cmd::Id id)
{
    switch (id) {
        case Cmd::Id::INIT:       return "init";
        case Cmd::Id::DEINIT:     return "deinit";
        case Cmd::Id::START:      return "start";
        case Cmd::Id::STOP:       return "stop";
        case Cmd::Id::NONE:
        default:                  break;
    }

    return "unknown";
}

bool BaroMs5837::isSync(Cmd::Id id)
{
    return (id == Cmd::Id::INIT) || (id == Cmd::Id::DEINIT);
}

bool BaroMs5837::sendSync(Cmd& cmd)
{
    // Non-blocking: a queue with no space is a worker that is not draining
    // it, so waiting for room would never be answered. A queue that was never
    // created fails the same way.
    if (!mCmdQueue.push(cmd, 0)) {
        LOG_ERROR("Dropped the %s command, queue full or not created\n",
                describe(cmd.id));
        return false;
    }

    // Unbounded: a queued command is always answered. A failed pop is not an
    // answer.
    bool ok = false;

    if (!mSyncReply.pop(ok)) {
        return false;
    }

    return ok;
}

void BaroMs5837::handleSync(bool ok)
{
    // Never block the worker on its caller. The slot is always empty here -
    // one answer per command, one waiter at a time - so a refusal means that
    // pairing broke and a caller is parked for good.
    if (!mSyncReply.push(ok, 0)) {
        LOG_ERROR("Could not answer a blocking command, reply slot full\n");
    }
}

bool BaroMs5837::sendAsync(const Cmd& cmd)
{
    // Never block a caller: a streaming request is not worth stalling for.
    if (!mCmdQueue.push(cmd, 0)) {
        LOG_ERROR("Dropped the %s command, queue full or not created\n",
                describe(cmd.id));
        return false;
    }

    return true;
}

void BaroMs5837::thread()
{
    LOG_INFO("Worker started\n");

    while (!mThread.shouldStop()) {

        // On every turn, a command or a wait run out alike: the wait below is
        // measured to the moment this next has something to do.
        service();

        Cmd cmd {};

        if (!mCmdQueue.pop(cmd, waitMs())) {
            continue;
        }

        if (mThread.shouldStop()) {
            // The stop took this command with it. A synchronous caller has no
            // deadline of its own, so it is answered as a failure.
            if (isSync(cmd.id)) {
                handleSync(false);
            }

            break;
        }

        switch (cmd.id) {
            case Cmd::Id::INIT:
                hInit();
                break;

            case Cmd::Id::DEINIT:
                hDeinit();
                break;

            case Cmd::Id::START:
                hStart(cmd);
                break;

            case Cmd::Id::STOP:
                hStop();
                break;

            case Cmd::Id::NONE:
            default:
                break;
        }
    }

    // Leave the part unpowered even when torn down mid-stream. The destructor
    // waits on the join, so there is no verdict.
    if (mIsInit.load(std::memory_order_relaxed)) {
        hStop();
    }

    LOG_INFO("Worker stopped\n");
}

void BaroMs5837::hInit()
{
    // Only records the bus and the address; nothing to report.
    mDrv.config(*mpI2c);

    // Driven, and low: the supply is off until a stream is wanted.
    mpEnable->init(Interface::IGpo::Type::PP, false, false);

    // More than once: a part called absent here stays absent until the next
    // boot, so one failed probe is not taken for the answer.
    bool found = false;

    for (uint32_t attempt = 0; (attempt < skProbeAttempts) && !found; attempt++) {
        found = powerUp();
    }

    powerDown();

    mIsInit.store(found, std::memory_order_release);

    if (found) {
        LOG_INFO("Initialized\n");
    } else {
        LOG_ERROR("No MS5837 found\n");
    }

    handleSync(found);
}

void BaroMs5837::hDeinit()
{
    hStop();

    // The pin is not released: left undriven it would float, and the supply
    // with it. Low is the off state it is kept in.

    mIsInit.store(false, std::memory_order_release);

    handleSync(true);

    LOG_INFO("Deinitialized\n");
}

void BaroMs5837::hStart(const Cmd& cmd)
{
    // Not redundant with requestStart(): a deinit can be handled between that
    // check and this command.
    if (!mIsInit.load(std::memory_order_relaxed)) {
        LOG_WARNING("START before init\n");
        return;
    }

    const uint32_t periodMs = BaroCadence::periodMsFor(cmd.sampleRateHz);
    const uint32_t periodUs = periodMs * 1000U;

    // A START on a stream already wanted is a rate change, whether the part
    // is measuring or being recovered: the power is not touched and the
    // attempts spent on a fault stand.
    if (mRecovery.wanted) {
        if (periodUs != mAppliedPeriodUs) {
            mAppliedPeriodUs = periodUs;
            mCadence.retime(periodMs);

            LOG_INFO("Period changed to %u ms\n", static_cast<unsigned>(periodMs));
        }

        return;
    }

    // Owned and recorded before the attempt, so a start that fails is
    // recovered at the rate asked for.
    mRecovery        = BaroRecoveryPolicy::State {};
    mRecovery.wanted = true;
    mRecovering      = false;
    mAppliedPeriodUs = periodUs;

    mRecovery.up = powerUp();

    if (!mRecovery.up) {
        LOG_ERROR("START failed, recovery will retry\n");
        return;
    }

    mCadence.start(Driver::System::GetTimeMs(), periodMs);
    mIsRunning.store(true, std::memory_order_release);

    // The rung, not the request.
    LOG_INFO("Streaming at %u ms\n", static_cast<unsigned>(periodMs));
}

void BaroMs5837::hStop()
{
    const bool wasWanted = mRecovery.wanted;

    mRecovery.wanted = false;

    mCadence.stop();
    powerDown();

    if (wasWanted) {
        LOG_INFO("Stopped\n");
    }
}

void BaroMs5837::service()
{
    if (!mRecovery.wanted) {
        return;
    }

    const uint32_t now = Driver::System::GetTimeMs();

    if (!mRecovering) {
        const BaroRecoveryPolicy::Action action =
                BaroRecoveryPolicy::decide(mRecovery);

        if (action == BaroRecoveryPolicy::Action::NOTHING) {
            if (mCadence.take(now)) {
                measure();
            }

            return;
        }

        if (action == BaroRecoveryPolicy::Action::GIVE_UP) {
            LOG_ERROR("Part did not come back after %u power cycles, giving up\n",
                    static_cast<unsigned>(BaroRecoveryPolicy::skMaxAttempts));

            hStop();
            return;
        }

        beginRecovery(now);
    }

    // Compared as a difference, so a wait is waited out whatever count it
    // started at. Where the millisecond clock turns over, short of 2^32, the
    // wait reads as long over and this one attempt is made early.
    if (static_cast<int32_t>(now - mRetryAtMs) >= 0) {
        attemptRecovery();
    }
}

uint32_t BaroMs5837::waitMs() const
{
    // Parked when no stream is wanted.
    if (!mRecovery.wanted) {
        return osWaitForever;
    }

    const uint32_t now = Driver::System::GetTimeMs();

    if (mRecovering) {
        const int32_t left = static_cast<int32_t>(mRetryAtMs - now);

        return (left > 0) ? static_cast<uint32_t>(left) : 0U;
    }

    // A fault found by the last measurement, or a stream to give up on.
    if (BaroRecoveryPolicy::decide(mRecovery)
            != BaroRecoveryPolicy::Action::NOTHING) {
        return 0U;
    }

    return mCadence.dueInMs(now);
}

void BaroMs5837::measure()
{
    // The instant both channels carry: before the bus, so that neither the
    // conversions nor a wait for the bus are in it.
    const uint32_t startMs = Driver::System::GetTimeMs();

    float                 degC = 0.0f;
    float                 mbar = 0.0f;
    Hardware::MS5837::Raw raw;

    if (!mDrv.get(degC, mbar, raw)) {
        BaroRecoveryPolicy::onReadFailed(mRecovery);
        return;
    }

    const bool repeated = mHaveLastRaw
                       && (raw.d1 == mLastRaw.d1)
                       && (raw.d2 == mLastRaw.d2);

    mLastRaw     = raw;
    mHaveLastRaw = true;

    const uint32_t spent = mRecovery.attempts;

    BaroRecoveryPolicy::onReadOk(mRecovery, repeated);

    // Said once the policy takes the part for measuring again.
    if ((spent != 0U) && (mRecovery.attempts == 0U)) {
        LOG_INFO("Measuring again after %u power cycles\n",
                static_cast<unsigned>(spent));
    }

    // A repeat is handed on like any other measurement: a part that converts
    // returns the same words now and then, and a run of them is the policy's
    // to answer.
    deliver(static_cast<uint64_t>(startMs) * 1000U, mbar, degC);
}

void BaroMs5837::beginRecovery(uint32_t nowMs)
{
    // Said once per fault: the attempts after the first stay quiet.
    if (mRecovery.attempts == 0U) {
        LOG_WARNING("Part stopped measuring, power cycling it\n");
    }

    mCadence.stop();
    powerDown();

    mRecovering = true;
    mRetryAtMs  = nowMs + BaroRecoveryPolicy::waitMs(mRecovery.attempts);
}

void BaroMs5837::attemptRecovery()
{
    mRecovering = false;

    const bool up = powerUp();

    BaroRecoveryPolicy::onPowerCycled(mRecovery, up);

    if (!up) {
        return;
    }

    mCadence.start(Driver::System::GetTimeMs(), mAppliedPeriodUs / 1000U);
    mIsRunning.store(true, std::memory_order_release);
}

bool BaroMs5837::powerUp()
{
    // Off first, whatever the pin was: a part that is up but stuck needs its
    // supply taken away, and one that was off loses nothing by it.
    mpEnable->set(false);
    OS::Delay(skPowerOffMs);

    mpEnable->set(true);
    OS::Delay(skPowerOnMs);

    // Reset, calibration read and its checksum, and the model check.
    return mDrv.init();
}

void BaroMs5837::powerDown()
{
    mIsRunning.store(false, std::memory_order_release);

    mpEnable->set(false);
}

void BaroMs5837::deliver(uint64_t timestampUs, float mbar, float degC)
{
    DataCallback* pCb = mCallback.load(std::memory_order_acquire);
    if (pCb == nullptr) {
        return;
    }

    // Set on this thread where the rate is taken, so it is the period this
    // measurement was made at.
    pCb->onPressure(timestampUs, mbar, mAppliedPeriodUs);
    pCb->onTemperature(timestampUs, degC, mAppliedPeriodUs);
}
