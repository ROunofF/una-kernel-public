/**
 ******************************************************************************
 * @file    MagBosch.cpp
 * @brief   Magnetometer component (BMM350).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagBosch.hpp"

#define LOG_MODULE_PRX      "MagBosch"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "System.hpp"

namespace {

// How the part is mounted, as the rotation that takes its frame to the
// watch's: row i makes the watch's axis i out of the part's three. The
// watch's frame is the one sensor data is published in: +X out of the
// 3 o'clock side, +Y out of the 12 o'clock side, +Z out of the face.
// Measured on the watch. A correction is valid only in the frame it was
// fitted in, so a change here makes every stored one wrong.
constexpr float kMount[3][3] = {
    { 0.0f, 1.0f,  0.0f },
    { 1.0f, 0.0f,  0.0f },
    { 0.0f, 0.0f, -1.0f },
};

// A mounting is a rotation, with determinant +1. Anything else mirrors the
// field, and a mirrored field gives a bearing that counts backwards.
constexpr float kMountDet =
        (kMount[0][0] * ((kMount[1][1] * kMount[2][2])
                       - (kMount[1][2] * kMount[2][1])))
      - (kMount[0][1] * ((kMount[1][0] * kMount[2][2])
                       - (kMount[1][2] * kMount[2][0])))
      + (kMount[0][2] * ((kMount[1][0] * kMount[2][1])
                       - (kMount[1][1] * kMount[2][0])));

static_assert(kMountDet > 0.0f,
        "the mount mirrors the field rather than turning it");

} // namespace

MagBosch::MagBosch()
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
    // Above normal priority: the part has no FIFO, so a late servicer loses
    // samples rather than delaying them.
    bool threadOk = false;
    if (queueOk) {
        threadOk = mThread.init(LOG_MODULE_PRX, this, &MagBosch::thread,
                osPriorityAboveNormal);
        if (!threadOk) {
            LOG_ERROR("Worker thread failed to start\n");
        }
    }

    mUsable = queueOk && replyOk && mutexOk && threadOk;
}

MagBosch::~MagBosch()
{
    // The command queue is a registered cancellable, so this also unblocks
    // the pop() the worker is parked on.
    mThread.requestStop();

    mThread.join();

    // The worker has parked, and on its way out suspended the chip and gave
    // the pin back.
    mThread.deinit();
    mCmdQueue.deinit();
    mSyncReply.deinit();
    mLifecycleMutex.deinit();
}

bool MagBosch::config(Interface::II2c& i2c, Interface::IExti& intPin)
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

    mpI2c    = &i2c;
    mpIntPin = &intPin;
    mConfigured = true;

    return true;
}

bool MagBosch::init()
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

void MagBosch::deinit()
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

bool MagBosch::isInit() const
{
    return mIsInit.load(std::memory_order_acquire);
}

void MagBosch::setDataCallback(DataCallback* pCb)
{
    mCallback.store(pCb, std::memory_order_release);
}

bool MagBosch::requestStart(float sampleRateHz)
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

bool MagBosch::requestStop()
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

bool MagBosch::isRunning() const
{
    return mIsRunning.load(std::memory_order_acquire);
}

float MagBosch::rungToRateHz(Hardware::BMM350::Odr odr)
{
    return 1000000.0f / static_cast<float>(Hardware::BMM350::odrToPeriodUs(odr));
}

float MagBosch::getMaxSampleRateHz() const
{
    // Both ends come off the driver's own ladder rather than being restated.
    return rungToRateHz(Hardware::BMM350::Odr::ODR_400_HZ);
}

float MagBosch::getMinSampleRateHz() const
{
    return rungToRateHz(Hardware::BMM350::Odr::ODR_1_5625_HZ);
}

const char* MagBosch::describe(Cmd::Id id)
{
    switch (id) {
        case Cmd::Id::INIT:       return "init";
        case Cmd::Id::DEINIT:     return "deinit";
        case Cmd::Id::START:      return "start";
        case Cmd::Id::STOP:       return "stop";
        case Cmd::Id::DATA_RDY:   return "data ready";
        case Cmd::Id::NONE:
        default:                  break;
    }

    return "unknown";
}

bool MagBosch::isSync(Cmd::Id id)
{
    return (id == Cmd::Id::INIT) || (id == Cmd::Id::DEINIT);
}

bool MagBosch::sendSync(Cmd& cmd)
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

void MagBosch::handleSync(bool ok)
{
    // Never block the worker on its caller. The slot is always empty here -
    // one answer per command, one waiter at a time - so a refusal means that
    // pairing broke and a caller is parked for good.
    if (!mSyncReply.push(ok, 0)) {
        LOG_ERROR("Could not answer a blocking command, reply slot full\n");
    }
}

bool MagBosch::sendAsync(const Cmd& cmd)
{
    // Never block a caller: a streaming request is not worth stalling for.
    if (!mCmdQueue.push(cmd, 0)) {
        LOG_ERROR("Dropped the %s command, queue full or not created\n",
                describe(cmd.id));
        return false;
    }

    return true;
}

void MagBosch::thread()
{
    LOG_INFO("Worker started\n");

    while (!mThread.shouldStop()) {

        // On every turn: a stream whose interrupts keep coming while no
        // sample comes out of them never lets the queue go quiet.
        hWatchdog();

        Cmd cmd {};

        // Parked when no stream is wanted; otherwise woken as often as the
        // watchdog is willing to look.
        const uint32_t timeout = mStreamWanted
                ? MagRecoveryPolicy::waitMs(mAppliedPeriodUs, mRecoveryAttempts)
                : osWaitForever;


        if (!mCmdQueue.pop(cmd, timeout)) {
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

            case Cmd::Id::DATA_RDY:
                hDataReady(cmd);
                break;

            case Cmd::Id::NONE:
            default:
                break;
        }
    }

    // Leave the part powered down and the pin released even when torn down
    // mid-stream. The destructor waits on the join, so there is no verdict.
    if (mIsInit.load(std::memory_order_relaxed)) {
        releaseChip();
    }

    LOG_INFO("Worker stopped\n");
}

void MagBosch::hInit()
{
    // Only records the bus and the address; nothing to report.
    mDrv.configure(*mpI2c, Hardware::BMM350::skI2cAddr);

    bool ok = false;

    do {
        // init() checks the chip id too. Probing first skips a reset and OTP
        // download against an empty address and logs a missing part as such.
        if (!mDrv.isReady()) {
            LOG_ERROR("No BMM350 at 0x%02X\n", Hardware::BMM350::skI2cAddr);
            break;
        }

        if (!mDrv.init()) {
            break;
        }

        // Lets the part release a stalled transfer, which matters on a shared
        // bus. Not fatal if it fails.
        if (!mDrv.setI2cWatchdog(true, false)) {
            LOG_WARNING("Could not arm the on-chip I2C watchdog\n");
        }

        // Pull down until the chip drives INT. Rising edge to match the
        // active-high latched configuration applied when streaming starts.
        mpIntPin->init(Interface::IGpi::Pull::DOWN,
                       Interface::IExti::Edge::RISING,
                       skIrqPriority);
        mpIntPin->setExtiHandler(this);

        // Configuring the line can leave an edge pending on it.
        mpIntPin->clearInt();

        // Masked explicitly: the pin interface does not say what state init()
        // leaves the interrupt in. Streaming arms it.
        mpIntPin->enableInt(false);

        ok = true;
    } while (false);

    mIsInit.store(ok, std::memory_order_release);

    if (ok) {
        LOG_INFO("Initialized\n");
    }

    handleSync(ok);
}

void MagBosch::hDeinit()
{
    releaseChip();

    // Anything still queued was aimed at a part that is now down; dropped so
    // the next init() does not open with this session's tail. After
    // releaseChip() has detached the handler, so no data-ready lands behind.
    mCmdQueue.clear();

    handleSync(true);

    LOG_INFO("Deinitialized\n");
}

void MagBosch::releaseChip()
{
    hStop();

    // Release only what was taken: the pin is claimed as the last step of an
    // init that succeeded, so a teardown after a failed one leaves it alone.
    if (mIsInit.load(std::memory_order_relaxed)) {
        mpIntPin->setExtiHandler(nullptr);
        mpIntPin->deinit();
    }

    mIsInit.store(false, std::memory_order_release);
}

void MagBosch::hStart(const Cmd& cmd)
{
    // Not redundant with requestStart(): a deinit can be handled between that
    // check and this command.
    if (!mIsInit.load(std::memory_order_relaxed)) {
        LOG_WARNING("START before init\n");
        return;
    }

    // A START on a running stream is a rate change. The part applies a new
    // rate while running, so a rebuild would only lose samples.
    if (mIsRunning.load(std::memory_order_relaxed)) {
        mWantedRateHz = cmd.sampleRateHz;

        const uint32_t oldPeriodUs = mAppliedPeriodUs;

        if (!programRate(cmd.sampleRateHz)) {
            LOG_ERROR("Rate change to %f Hz failed\n", cmd.sampleRateHz);
        } else if (mAppliedPeriodUs != oldPeriodUs) {
            // The conversion in flight is finished at the old rate.
            mRateHandover.changed(oldPeriodUs);
        }

        return;
    }

    // Owned and recorded before the attempt, so a start that fails is
    // recovered at the rate asked for.
    mStreamWanted     = true;
    mWantedRateHz     = cmd.sampleRateHz;
    mLastSampleMs     = Driver::System::GetTimeMs();
    mRecoveryAttempts = 0;

    if (!applyStreamingConfig(cmd.sampleRateHz)) {
        LOG_ERROR("START failed, recovery will retry\n");
        return;
    }

    mIsRunning.store(true, std::memory_order_release);

    // The rung, not the request.
    LOG_INFO("Streaming at %u us\n", static_cast<unsigned>(mAppliedPeriodUs));
}

void MagBosch::hWatchdog()
{
    const uint32_t now = Driver::System::GetTimeMs();


    const MagRecoveryPolicy::State state {
        mStreamWanted,
        mIsRunning.load(std::memory_order_relaxed),
        now - mLastSampleMs,
        mRecoveryAttempts,
        mAppliedPeriodUs,
    };

    const MagRecoveryPolicy::Action action = MagRecoveryPolicy::decide(state);

    if (action == MagRecoveryPolicy::Action::NOTHING) {
        return;
    }

    // Through hStop(): a chip configured by the last attempt may still be
    // converting into a masked pin, with isRunning() reporting true.
    if (action == MagRecoveryPolicy::Action::GIVE_UP) {
        LOG_ERROR("Stream did not come back after %u attempts, giving up\n",
                static_cast<unsigned>(MagRecoveryPolicy::skMaxAttempts));

        hStop();
        return;
    }

    // Either action is an attempt and restarts the clock, so a fix gets a
    // full wait to show for itself.
    mLastSampleMs = now;
    mRecoveryAttempts++;

    if (action == MagRecoveryPolicy::Action::RELEASE_LATCH) {
        LOG_WARNING("No samples for %u ms, releasing the interrupt\n",
                static_cast<unsigned>(state.sinceLastSampleMs));

        bool ready = false;
        (void) mDrv.getDataReady(ready);

        return;
    }

    // Rebuilt at the rate asked for, not the one the chip holds. Warned about
    // only when the stream was up on the way in, so failing restarts stay
    // quiet; the attempt limit bounds both.
    const bool wasRunning = mIsRunning.load(std::memory_order_relaxed);

    if (wasRunning) {
        LOG_WARNING("Stream is not recovering, restarting it\n");
    }

    stopStreaming();

    if (!applyStreamingConfig(mWantedRateHz)) {
        // Left stopped; mStreamWanted survives, so the next wait retries.
        if (wasRunning) {
            LOG_ERROR("Stream restart failed\n");
        }
        return;
    }

    mIsRunning.store(true, std::memory_order_release);

    LOG_INFO("Stream restarted at %u us\n",
            static_cast<unsigned>(mAppliedPeriodUs));
}

void MagBosch::hStop()
{
    // Here and not in stopStreaming(): recovery goes through that one and
    // keeps the stream wanted.
    mStreamWanted = false;

    stopStreaming();
}

void MagBosch::stopStreaming()
{
    // Nothing ties the part's counter to the system clock across a stop,
    // so the next stream is tied afresh.
    mTimeBase.reset();

    if (!mIsRunning.exchange(false, std::memory_order_release)) {
        return;
    }

    mpIntPin->enableInt(false);

    mDrv.enableInterrupt(false);

    // Release a latch the last unread edge may have left, or the next start
    // sees no rising edge. After the chip's output is off, so nothing asserts
    // behind the read, and before the suspend.
    bool ready = false;
    (void) mDrv.getDataReady(ready);

    mDrv.setPowerMode(Hardware::BMM350::PowerMode::SUSPEND);

    LOG_INFO("Stopped\n");
}

void MagBosch::hDataReady(const Cmd& cmd)
{
    // Queued before a STOP: in suspend a read would return a stale sample.
    if (!mIsRunning.load(std::memory_order_relaxed)) {
        return;
    }

    // Raised before this stream's epoch: whatever a read took now would be a
    // later conversion than the one the edge announced.
    if (cmd.epoch != mEdgeEpoch.load(std::memory_order_acquire)) {
        return;
    }

    // Only steers the time base, and only while it belongs to a sample.
    uint32_t edgeMs = cmd.timestampMs;

    for (uint32_t drain = 0; drain < skPinDrainGuard; drain++) {

        // Status first: on a latched pin this is what releases INT.
        bool ready = false;
        if (!mDrv.getDataReady(ready)) {
            // DEBUG: the watchdog logs the resulting stall.
            LOG_DEBUG("Could not read the data-ready status\n");
            break;
        }

        if (!ready) {
            // No conversion behind the edge; the read released the latch.
            break;
        }

        Hardware::BMM350::Sample sample;
        if (!mDrv.readSample(sample)) {
            // DEBUG: a failing read repeats on every conversion, and the
            // watchdog still sees the stall.
            LOG_DEBUG("Could not read the sample\n");
            break;
        }

        // The watchdog's proof of life, and the latest instant the sample may
        // carry: taken at the read, not the delivery.
        const uint32_t readMs = Driver::System::GetTimeMs();

        mLastSampleMs     = readMs;
        mRecoveryAttempts = 0;

        // A stream's first sample places it in time, so it is delivered only
        // if its own edge announced it; one read late, as when this thread
        // was held up, and those drained behind it are dropped, and the next
        // edge places the stream. A drained sample is read later still than
        // the one before it, so it never passes where that one did not.
        bool placing = false;

        if (!mStreamPlaced) {
            if (edgeAnnouncedRead(cmd.timestampMs, readMs, mAppliedPeriodUs)) {
                mStreamPlaced = true;
                placing       = true;
            } else {
                // DEBUG: only until a stream is placed, a sample or two.
                LOG_DEBUG("Sample read %lu ms after its edge, not placing the stream\n",
                          static_cast<unsigned long>(readMs - cmd.timestampMs));
            }
        }

        // The status read releases the latch; a conversion completing before
        // the data read raises the line again, and the loop reads status and
        // data once more, the same conversion. Delivered twice, it would be
        // two samples a microsecond apart. The conversion it overwrote before
        // the first read is lost either way: the part has no FIFO.
        const bool repeated = mStreamPlaced && !placing
                           && (sample.sensorTime == mLastSensorTime);

        if (mStreamPlaced && !repeated) {
            mLastSensorTime = sample.sensorTime;

            const uint64_t at = mTimeBase.stamp(sample.sensorTime, edgeMs, readMs,
                                                mAppliedPeriodUs);

            // Counted by the time base either way, but with no instant both
            // after the last one and no later than its read, not delivered.
            if (at != MagTimeBase::skWithheld) {
                deliver(at, sample);
            }
        }

        // Samples drained behind this one have no edge of their own.
        edgeMs = 0;

        // High again only if another conversion completed during the read.
        if (!mpIntPin->get()) {
            break;
        }
    }
}

bool MagBosch::edgeAnnouncedRead(uint32_t edgeMs, uint32_t readMs,
                                 uint32_t periodUs)
{
    const uint64_t lateUs = static_cast<uint64_t>(readMs - edgeMs) * 1000U;

    return lateUs <= ((periodUs / 2U) + 1000U);
}

bool MagBosch::programRate(float sampleRateHz)
{
    // Clamped into the ladder first, so the division below is safe. Written
    // as a failed lower-bound test so NaN is caught too.
    if (!(sampleRateHz >= getMinSampleRateHz())) {
        sampleRateHz = getMinSampleRateHz();
    } else if (sampleRateHz > getMaxSampleRateHz()) {
        sampleRateHz = getMaxSampleRateHz();
    }

    // periodUsToOdr() picks the slowest rung whose period fits, so the part
    // never samples less often than asked. Truncating the period, not
    // rounding it, preserves that.
    const uint32_t periodUs = static_cast<uint32_t>(1000000.0f / sampleRateHz);

    const Hardware::BMM350::Odr odr = Hardware::BMM350::periodUsToOdr(periodUs);

    if (!mDrv.setOdr(odr, skAveraging)) {
        return false;
    }

    // From the rung, once the chip has taken it.
    mAppliedPeriodUs = Hardware::BMM350::odrToPeriodUs(odr);

    return true;
}

bool MagBosch::applyStreamingConfig(float sampleRateHz)
{
    // All or nothing: on false mIsRunning stays false and stopStreaming()
    // returns early, so anything left on would never be turned off.
    bool ok = false;

    do {
        if (!programRate(sampleRateHz)) {
            break;
        }

        // Latched: the drain loop in hDataReady() polls the pin level.
        Hardware::BMM350::InterruptConfig intConfig;
        intConfig.latched    = true;
        intConfig.activeHigh = true;
        intConfig.pushPull   = true;

        if (!mDrv.configureInterrupt(intConfig)) {
            break;
        }

        if (!mDrv.enableInterrupt(true)) {
            break;
        }

        // Release a latch left by whatever ran before this object: arming
        // catches edges, and a line already high raises none. Failure costs
        // only a start that never delivers.
        bool ready = false;
        (void) mDrv.getDataReady(ready);

        // Armed before the part converts, or the first edge can be missed for
        // good. clearInt() drops the controller's pending edge; the read above
        // dropped the chip's.
        mpIntPin->clearInt();
        mpIntPin->enableInt(true);

        if (!mDrv.setPowerMode(Hardware::BMM350::PowerMode::NORMAL)) {
            break;
        }

        // The switch waits out the part's start-up, tens of milliseconds,
        // and the part converts meanwhile: an edge raised then was stamped
        // long before this thread could read, and the read takes a later
        // conversion than the one it announced. The stream starts after the
        // wait: a new epoch retires the edges already queued, the stream is
        // unplaced and its time base untied, then the controller's pending
        // edge is dropped and the chip's latch released, in that order, so an
        // edge after the release is kept.
        mEdgeEpoch.fetch_add(1U, std::memory_order_release);
        mStreamPlaced = false;
        mTimeBase.reset();
        mRateHandover.start();
        mpIntPin->clearInt();
        (void) mDrv.getDataReady(ready);

        ok = true;
    } while (false);

    if (!ok) {
        // Undo whatever landed; safe on a part that never got that far.
        mpIntPin->enableInt(false);
        mDrv.enableInterrupt(false);
        mDrv.setPowerMode(Hardware::BMM350::PowerMode::SUSPEND);
    }

    return ok;
}

void MagBosch::deliver(uint64_t timestampUs,
        const Hardware::BMM350::Sample& sample)
{
    // The one place the part's axes become the watch's.
    const float rawX = (kMount[0][0] * sample.x) + (kMount[0][1] * sample.y)
                     + (kMount[0][2] * sample.z);
    const float rawY = (kMount[1][0] * sample.x) + (kMount[1][1] * sample.y)
                     + (kMount[1][2] * sample.z);
    const float rawZ = (kMount[2][0] * sample.x) + (kMount[2][1] * sample.y)
                     + (kMount[2][2] * sample.z);

    // The rate this sample was converted at: the one programRate() set on
    // this thread, or the one before it for a conversion that was in flight
    // when it did. Asked for every sample, subscriber or none, as it goes by
    // the sample before.
    const uint32_t periodUs = mRateHandover.periodFor(timestampUs, mAppliedPeriodUs);

    DataCallback* pCb = mCallback.load(std::memory_order_acquire);
    if (pCb == nullptr) {
        return;
    }

    pCb->onMagnetic(timestampUs, rawX, rawY, rawZ, periodUs);
}

void MagBosch::extiHandler(Interface::IExti* pExti,
        Interface::IExti::Edge edge)
{
    (void) pExti;
    (void) edge;

    Cmd cmd {};
    cmd.id = Cmd::Id::DATA_RDY;

    // Stamped here, not at the read, which the bus can hold off. Driver::System
    // rather than the OS wrapper: only it catches the clock up after STOP2.
    cmd.timestampMs = Driver::System::GetTimeMs();
    cmd.epoch       = mEdgeEpoch.load(std::memory_order_relaxed);

    // Zero timeout: a put from interrupt context allows no wait. A drop stops
    // the stream, since the line stays latched, until the watchdog releases it.
    (void) mCmdQueue.push(cmd, 0);
}
