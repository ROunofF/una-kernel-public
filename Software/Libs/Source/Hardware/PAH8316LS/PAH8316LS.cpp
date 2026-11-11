/**
******************************************************************************
 * @file    PAH8316LS.cpp
 * @brief   Body sensor Driver with Built-In Library.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "PAH8316LS.hpp"
#define LOG_MODULE_PRX      "PAH8316LS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>


#define INTERRUPT_DRIVEN

namespace Hardware
{

bool PAH8316LS::config(Interface::II2c &i2c,
                       Interface::IExti &intPin,
                       Interface::IGpo &sensEn,
                       Interface::IGpo &ledEn,
                       uint8_t devAddr)
{
    mpI2c = &i2c;
    mpIntPin = &intPin;
    mpSensEn = &sensEn;
    mpLedEn = &ledEn;

    mIsConfigured = true;

    return true;
}

bool PAH8316LS::init()
{
    LOG_INFO("Initialization\n");

    if (!mIsConfigured) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    InitState expected = InitState::None;
    if (!mState.compare_exchange_strong(expected, InitState::InProgress)) {
        LOG_WARNING("Already initializing or initialized. State %u\n", expected);
        return false;
    }

    osMemoryPoolAttr_t attr {};
    attr.name = "PAH.Algo";
    attr.attr_bits = 0;
    attr.cb_mem = &mMemoryPoolCb;
    attr.cb_size = sizeof(StaticMemPool_t);
    attr.mp_mem = storage;
    attr.mp_size = sizeof(storage);
    mMemoryPoolId = osMemoryPoolNew(skAlgoSampleOffset, sizeof(BodySensorData_t), &attr);

    mAlgoSampleQueue.init("PAH.Algo");

    mCmdQueue.init("PAH8316LS");

    mpSensEn->init(Interface::IGpo::Type::PP, false, false);
    mpLedEn->init(Interface::IGpo::Type::PP, false, false);
    mpIntPin->init(Interface::IGpi::Pull::DOWN, Interface::IExti::Edge::RISING, 5);
    mpIntPin->enableInt(false);
    mpIntPin->setExtiHandler(this);

    if (!mThread.init("PAH8316LS", this, &PAH8316LS::thread, osPriorityBelowNormal, 5000)) {
        LOG_ERROR("mThread.init failed\n");

        mpIntPin->setExtiHandler(nullptr);
        mpIntPin->deinit();
        mpLedEn->deinit();
        mpSensEn->deinit();
        mCmdQueue.deinit();
        mState.store(InitState::None);
        return false;
    }

    mState.store(InitState::Initialized);
    LOG_DEBUG("Done\n");
    return true;
}

bool PAH8316LS::deinit()
{
    LOG_INFO("Deinitialization\n");

    if (!mIsConfigured) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    InitState expected = InitState::Initialized;
    if (!mState.compare_exchange_strong(expected, InitState::None)) {
        LOG_WARNING("Not initialized or already deinitialized. State %u\n", expected);
        return true;
    }

    mThread.requestStop();
    if(!mThread.join(1000)) {
        LOG_ERROR("mThread.join timeout\n");
    }
    mThread.deinit();

    resetState();
    mId = 0;

    mpIntPin->setExtiHandler(nullptr);
    mpIntPin->deinit();
    mpLedEn->deinit();
    mpSensEn->deinit();

    mCmdQueue.deinit();

    mAlgoSampleQueue.deinit();
    osMemoryPoolDelete(mMemoryPoolId);


    LOG_DEBUG("Done\n");
    return true;
}

bool PAH8316LS::isInitialized() const
{
    return mState.load() == InitState::Initialized;
}

bool PAH8316LS::enable(OperateMode mode)
{
    LOG_DEBUG("called\n");
    Cmd cmd {};
    cmd.id = Cmd::Id::START;
    cmd.mode = mode;
    return sendCommandToQueue(cmd);
}

bool PAH8316LS::disable()
{
    LOG_DEBUG("called\n");
    Cmd cmd {};
    cmd.id = Cmd::Id::STOP;
    return sendCommandToQueue(cmd);
}

void PAH8316LS::setAccRange(uint8_t range)
{
    mAccRange = range;

    // MEMS mode 0:2G, 1:4G, 2:8G
    uint8_t r = 0;
    switch(range) {
        case 8:  r = 2; break;
        case 4:  r = 1; break;
        case 2:  r = 0; break;
        default: assert(false); break;
    }

    mHrSession.setGSensorMode(r);
}

void PAH8316LS::enableDataLogging(bool enable)
{
    if (!enable && mLogToFileEnable) {
        mLogToFile.stop();
    }
    mLogToFileEnable = enable;
}

bool PAH8316LS::isTouched()
{
    return mTouched;
}

void PAH8316LS::getHeartrate(uint8_t &hr, uint8_t &trustLevel)
{
    hr = mHr;
    trustLevel = mTrustLevel;
}

uint16_t PAH8316LS::getId()
{
    return mId;
}


void PAH8316LS::thread()
{
    // Test chip
    mpSensEn->set(true);
    OS::Delay(10);  // Waiting for Ready state
    platformInit();
    mpSensEn->set(false);
    if (mId == 0) {
        LOG_ERROR("Chip verification failed\n");
        mThread.setInitFailed();
        return;
    }

    // Initialization successful
    mThread.setInitialized();

    LOG_INFO("Initialized\n");

    // Register Queue for automatic interruption
    OS::CancellableGuard guard(&mThread, &mCmdQueue);

    // Main loop
    while (!mThread.shouldStop()) {

        Cmd cmd {};
        // Bounded wait: on a timeout (no command) run the staleness watchdog.
        // A real STOP/shutdown still arrives as a command and is handled below.
        if (!mCmdQueue.pop(cmd, skWatchdogTickMs)) {
            watchdogCheck();
            continue;
        }

        if (mThread.shouldStop()) {
            LOG_DEBUG("Interrupted\n");
            break;
        }

        LOG_DEBUG("cmd id=%u, irq=%u\n", cmd.id, mpIntPin->get());

        switch (cmd.id) {
            case Cmd::Id::START:
                start(cmd.mode);
                break;

            case Cmd::Id::STOP:
                stop();
                break;

            case Cmd::Id::DATA_RDY: {
                // Process algorithms
                pah_interrupt_handle();
                processAlgo();
                // Natural liveness: refresh the staleness tick and replenish the
                // recovery budget within the session (parity with the IMU zeroing
                // its counters on a healthy read).
                mLastDataReadyTick = OS::GetTimeMs();
                mRecoveryAttempts = 0;
                mLivenessFails = 0;
            } break;

            case Cmd::Id::NONE:
            default:
                break;
        }
    }

    stop();

    clearPool();

    mCmdQueue.clear();
    LOG_INFO("Deinitialized\n");
}

void PAH8316LS::extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge)
{
    if (!isInitialized()) {
        LOG_ERROR("Driver not initializes\n");
        return;
    }

    // Notify thread
    Cmd cmd {};
    cmd.id = Cmd::Id::DATA_RDY;

    if (!mCmdQueue.push(cmd, 0)) {
        LOG_ERROR("Failed to send cmd id=%u to the queue from interrupt\n", cmd.id);
    }
}

void PAH8316LS::start(OperateMode mode)
{
    mMode = mode;
    powerUp(mode);

    // Begin a streaming session. watchdogCheck() decides per-poll whether to
    // apply the time-based DATA_RDY staleness check, the touch-value heartbeat,
    // or neither -- from the mode, the touch state, and DATA_RDY freshness (see
    // watchdogCheck()). Invalidate the heartbeat baseline so the first probe of a
    // data gap seeds afresh against the just-started chip.
    mLastDataReadyTick = OS::GetTimeMs();
    mRecoveryAttempts = 0;
    mLivenessFails = 0;
    mTouchHbValid = false;
    mStreaming = true;
}

void PAH8316LS::powerUp(OperateMode mode)
{
    mpSensEn->set(true);
    mpLedEn->set(true);

    OS::Delay(10);  // Waiting for Ready state

    platformInit();

    // A failed chip verification leaves the vendor library with a zeroed copy
    // of the platform callbacks (pah831x_sensor_init() memsets its state and
    // returns before pah831x_init() ever copies them in), so any further
    // library call would jump through a NULL function pointer (field crash:
    // comm_write -> PC=0). Bail out here; the rails deliberately stay up
    // so the staleness watchdog remains armed and retries via the normal
    // bounded recovery path (recover() re-runs platformInit()).
    if (mId == 0) {
        LOG_ERROR("Chip verification failed, sensor not started\n");
        return;
    }

    clearPool();

    setAccRange(mAccRange); // Make sure that range is set

    pah831x_operate_mode operateMode = static_cast<pah831x_operate_mode>(mode);

    mHrSession.close();

    #if defined(INTERRUPT_DRIVEN)
    pah831x_sensor_start(PAH831X_WORK_DRI , operateMode,
                         PAH831X_PPG_REG_SETTING_1,
                         PAH831X_ECG_REG_SETTING_IDLE,
                         skPpgWatermark, (float)skSamplePeriod);
#else
    pah831x_sensor_start(PAH831X_WORK_POLLING,
                         PAH831X_INIT_AUTO_MODE,
                         PAH831X_PPG_REG_SETTING_1,
                         PAH831X_ECG_REG_SETTING_IDLE,
                         skPpgWatermark,50 );
#endif

    mpIntPin->enableInt(true);

    LOG_INFO("Started in mode %u, Expected accelerometer range %uG\n",
            mode, (1 << (mHrSession.gSensorMode() + 1)));
}

void PAH8316LS::watchdogCheck()
{
    // Inert unless a streaming session is active and the sensor rail is up.
    if (!mStreaming || !mpSensEn->get()) {
        return;
    }

    const uint32_t now = OS::GetTimeMs();

    // Fresh DATA_RDY means samples are flowing, so the measurement engine is
    // unambiguously alive: replenish the recovery budget, invalidate the
    // heartbeat baseline (so a later data gap re-seeds against a fresh value),
    // and skip the I2C heartbeat poll entirely while streaming.
    if ((now - mLastDataReadyTick) <= skStaleThresholdMs) {
        mRecoveryAttempts = 0;
        mLivenessFails = 0;
        mTouchHbValid = false;
        return;
    }

    // DATA_RDY has been silent past the threshold. Decide whether that is a fault
    // (wedge) or legitimate. The TIME-BASED silence check is a fault ONLY when
    // periodic DATA_RDY is guaranteed while healthy: AlwaysRun (PPG streams
    // continuously) and Auto WHILE WORN/touched (PPG duty-cycles on with skin
    // contact). In Auto with no touch the PPG is duty-cycled off and DATA_RDY is
    // legitimately silent -- an earlier assumption to the contrary power-cycled a
    // healthy chip every few seconds on an idle, unworn watch.
    bool wedge = false;
    const bool expectPeriodicData =
            (mMode == OperateMode::AlwaysRun) ||
            (mMode == OperateMode::Auto && mTouched);

    if (mId == 0) {
        // The last powerUp() failed chip verification, so the vendor library
        // never initialized (its callback copy is zeroed) - no probe can be
        // made through it. Go straight to the bounded recovery below, which
        // power-cycles the chip and re-runs the verification.
        LOG_WARNING("Chip not verified, forcing recovery\n");
        wedge = true;
    } else if (expectPeriodicData) {
        // Streaming was expected but DATA_RDY stopped: a wedge (full, or a
        // partial one where PPG died while touch still reports). Recover.
        LOG_WARNING("No DATA_RDY for %lu ms while streaming\n",
                    (unsigned long)(now - mLastDataReadyTick));
        wedge = true;
    } else {
        // Auto-untouched or TouchOnly: DATA_RDY silence is LEGITIMATE, so it must
        // NOT by itself trigger a power-cycle. Probe the measurement engine
        // directly via the touch raw value, which the touch detector rewrites
        // every report period (~10 Hz) regardless of contact and independently of
        // the DATA_RDY interrupt. An ADVANCING value proves the engine is alive;
        // a value frozen for skHeartbeatFrozenMs is the "Calculating..." wedge
        // (engine dead) -- including the from-start / never-touched case that a
        // DATA_RDY-only or touch-gated check misses. (On-device the untouched
        // value was confirmed to keep changing even in darkness.)
        uint16_t touchVal = 0;
        if (!get_pah831x_touch_value_checked(&touchVal)) {
            // The read itself failed: the chip is NAKing the bus. Tolerate a few
            // transient NAKs, then treat it as a dead chip.
            if (++mLivenessFails < skLivenessFailsForReset) {
                LOG_WARNING("Heartbeat read failed (%lu/%lu)\n",
                            (unsigned long)mLivenessFails,
                            (unsigned long)skLivenessFailsForReset);
                return;
            }
            LOG_ERROR("Chip not responding after %lu failed heartbeat reads\n",
                      (unsigned long)mLivenessFails);
            wedge = true;
        } else {
            mLivenessFails = 0;
            if (mTouchHbValid && touchVal == mLastTouchHb) {
                // Unchanged since the last poll. Only a SUSTAINED freeze is a
                // wedge -- healthy jitter changes the value well within the
                // window. The budget is NOT replenished by a no-change, so a
                // wedged-but-alive-on-bus chip stays bounded.
                if ((now - mLastTouchHbChangeTick) >= skHeartbeatFrozenMs) {
                    LOG_WARNING("Touch heartbeat frozen for %lu ms (engine wedged)\n",
                                (unsigned long)(now - mLastTouchHbChangeTick));
                    wedge = true;
                } else {
                    return;
                }
            } else {
                // First read of this data gap, or the value advanced: engine
                // alive. A genuine ADVANCE (valid baseline that changed)
                // replenishes the recovery budget; a first-read seed does NOT, so
                // a freeze persisting across a recovery cannot reset the cap.
                const bool advanced = mTouchHbValid;
                mLastTouchHb = touchVal;
                mLastTouchHbChangeTick = now;
                mTouchHbValid = true;
                if (advanced) {
                    mRecoveryAttempts = 0;
                }
                return;
            }
        }
    }

    if (!wedge) {
        return;
    }

    // A recovery is warranted. Bound the attempts so a genuinely dead chip does
    // not thrash the worker thread. The budget is replenished ONLY by genuinely
    // resumed liveness above (fresh DATA_RDY, or an ADVANCING touch value), never
    // by a recovery that "succeeds" (re-inits on the bus) without restoring data
    // -- so a chip that power-cycles clean but stays mute stops after
    // skMaxRecoveryAttempts instead of looping forever (the failure mode of the
    // original always-armed check).
    if (mRecoveryAttempts >= skMaxRecoveryAttempts) {
        return;  // Looks dead; stop thrashing until the next fresh START.
    }

    mRecoveryAttempts++;
    LOG_WARNING("Sensor wedge suspected, recovery attempt %lu/%lu\n",
                (unsigned long)mRecoveryAttempts,
                (unsigned long)skMaxRecoveryAttempts);

    if (recover()) {
        // Re-init on the bus succeeded. Deliberately do NOT reset mRecoveryAttempts
        // or mLastDataReadyTick here: only genuinely resumed data flow (the checks
        // above) may replenish the budget, otherwise a clean-but-mute power-cycle
        // would reset the cap and loop forever. Invalidate the heartbeat baseline
        // so it re-seeds against the freshly re-initialized chip.
        LOG_INFO("Recovery power-cycle done\n");
        mLivenessFails = 0;
        mTouchHbValid = false;
    } else if (mRecoveryAttempts >= skMaxRecoveryAttempts) {
        LOG_ERROR("Recovery exhausted after %lu attempts; giving up until next start\n",
                  (unsigned long)skMaxRecoveryAttempts);
    }
}

bool PAH8316LS::recover()
{
    // Software teardown mirroring stop() so the library and HR algorithm
    // re-initialize from a clean state. The HR session would otherwise carry on
    // over the wedge with stale algorithm state. pah_sensor_stop() goes through the
    // bounded I2C retry path, so it is safe even if the chip is NAKing.
    // NOTE: mStreaming intentionally stays true -- recovery is not a stop, the
    // staleness watchdog must remain armed for this streaming session.
    // Skip the library teardown when the last platformInit() failed chip
    // verification: the library never initialized (zeroed callback copy), so
    // there is no session to close and pah_sensor_stop() would call through a
    // NULL platform callback. mHrSession.close() does nothing when no session
    // is open.
    mpIntPin->enableInt(false);
    if (mId != 0) {
        pah_sensor_stop();
    }
    mHrSession.close();
    // resetState() clears mTouched (via resetData()). watchdogCheck() RELIES on
    // this: after a recovery an Auto session drops into the touch-value heartbeat
    // branch (mTouched == false) with its long freeze window, giving the freshly
    // re-initialized chip time to resume before the next wedge assessment --
    // rather than the expectPeriodicData path re-firing on the (deliberately not
    // reset) stale DATA_RDY tick within one poll. Do NOT preserve mTouched across
    // recovery or that grace is lost and a slow recovery-thrash can return.
    resetState();
    clearPool();

    // Sustained sensor-VDD power-down: drop both rails and hold them off long
    // enough (skMinOffMs) to cover rail bleed and the datasheet t6 (>=10 ms)
    // minimum so the chip performs a true power-on reset. A quick toggle is
    // insufficient because the rail must fully bleed.
    mpLedEn->set(false);
    mpSensEn->set(false);
    OS::Delay(skMinOffMs);

    // Clear any interrupt latched by the power-off edge before the EXTI is
    // re-armed inside powerUp(), so a bogus DATA_RDY can't inject and delay
    // re-detection (mirrors stop()).
    mpIntPin->clearInt();

    // Re-run the normal power-up + init sequence (platformInit() re-reads mId).
    powerUp(mMode);

    // powerUp() cannot fail-signal, so verify the chip actually came back.
    if (mId == 0) {
        LOG_ERROR("Chip verification failed during recovery\n");
        return false;
    }

    return true;
}

void PAH8316LS::stop()
{
    // Disarm the staleness watchdog: no data is expected while stopped.
    mStreaming = false;

    mpIntPin->enableInt(false);

    // As in recover(): no library session exists when the last platformInit()
    // failed chip verification, and calling into the library would go through
    // its zeroed callback copy.
    if (mId != 0) {
        pah_sensor_stop();
    }
    mHrSession.close();

    mpLedEn->set(false);
    mpSensEn->set(false);

    resetState();

    // It is necessary to clear the interrupt
    // that occurs when the chip is turned off.
    mpIntPin->clearInt();

    LOG_INFO("Stopped\n");
}

void PAH8316LS::processAlgo()
{
    if (!isInitialized() || !mpSensEn->get()) {
        LOG_WARNING("Wrong state: isInitialized %u, en %u\n", isInitialized(), mpSensEn->get());
        return;
    }

    bool touchChanged = false;
    bool hrChanged    = false;

    pah831x_sensor_task();

    if (get_ppg_data_ready()) {
        _ppg_data_out_alg = get_ppg_data_out();

        // Save new data
        BodySensorData_t* newSample = reinterpret_cast<BodySensorData_t*>(osMemoryPoolAlloc(mMemoryPoolId, 0));
        if (newSample == nullptr) {
            LOG_ERROR("No free items in pool\n");
            clearPool();
            // Try again
            newSample = reinterpret_cast<BodySensorData_t*>(osMemoryPoolAlloc(mMemoryPoolId, 0));
            if (newSample == nullptr) {
                LOG_ERROR("Failed to clear pool\n");
                return;
            }
        }

        newSample->startTimestamp = (uint32_t)_ppg_data_out_alg.start_timestamp;
        newSample->endTimestamp   = (uint32_t)_ppg_data_out_alg.end_timestamp;
        newSample->timePeriod     = _ppg_data_out_alg.duration;
        newSample->ppgLength      = _ppg_data_out_alg.sample_num_per_ch;
        newSample->channelNum     = _ppg_data_out_alg.ch_num;

        const size_t len = newSample->channelNum * newSample->ppgLength;

        memcpy(newSample->ppgData, _ppg_data_out_alg.ppg_raw_data, len * 4);
        memcpy(newSample->dacData, _ppg_data_out_alg.ppg_dac_data, len);

        newSample->accLength = 0;   // Must be fill in AccAidingCallback
        newSample->isTouched = _ppg_data_out_alg.is_touched; // TOUCH_STATUS__UNKNOWN, TOUCH_STATUS__ON, TOUCH_STATUS__OFF,
        newSample->logDataEn = mLogToFileEnable ? 1 : 0;

        // Put sample to queue for processing
        mAlgoSampleQueue.push(newSample, 0);


        // Process oldest samples only if queue is full
        BodySensorData_t* oldSample = nullptr;
        if (mAlgoSampleQueue.available() == 0) {
            mAlgoSampleQueue.pop(oldSample, 0);
        }

        if (oldSample) {
            BodySensorData_t& sd = *oldSample;

            // Get ACC data for time range of this sample
            if (mpAccRequestCallback) {
                uint32_t   startTime  = sd.startTimestamp;
                uint32_t   endTime    = sd.endTimestamp;
                uint32_t   minSamples = sd.ppgLength;
                uint32_t   maxSamples = skAccDataLength / sizeof(AccSample);
                uint32_t   count      = 0;
                AccSample* buff       = reinterpret_cast<AccSample*>(sd.accData);
                mpAccRequestCallback->onAccRequest(startTime, endTime, minSamples, maxSamples, buff, count);
                sd.accLength = count;

                LOG_DEBUG("Request acc data: time %u - %u, samples %u - %u, received %u samples\n",
                          startTime, endTime, minSamples, maxSamples, count);
            }

            // Run algorithm
            mHrSession.process(
                sd.timePeriod,
                sd.ppgData,
                sd.dacData,
                sd.ppgLength,
                sd.channelNum,
                sd.accData,
                sd.accLength,
                sd.isTouched,
                sd.logDataEn != 0,
                mHr,
                mTrustLevel
            );

            hrChanged = true;

            // Return processed sample to pool
            osMemoryPoolFree(mMemoryPoolId, oldSample);
        }

        clear_ppg_data_ready();
    }

    bool ppgState = get_ppg_enable() != 0;
    if (mPrevPpgState != ppgState) {
        mPrevPpgState = ppgState;
        LOG_DEBUG("PPG state changed %u. Reset data.\n", ppgState);

        resetData();
        touchChanged = true;
        hrChanged = true;

        if (!ppgState) {
            clearPool();
            mHrSession.close();
        }
    }


    if (get_pah831x_touch() == 1 && !mTouched) {
        mTouched = true;
        touchChanged = true;
    } else if (get_pah831x_touch() == 0 && mTouched) {
        mTouched = false;
        touchChanged = true;
    }


    if (touchChanged) {
        LOG_DEBUG("Touch: %u\n", mTouched);
        if (mpCallback) {
            mpCallback->onTouch(mTouched);
        }
    }

    if (hrChanged) {
        LOG_DEBUG("HR: %u, TrustLevel: = %u\n", mHr, mTrustLevel);
        if (mpCallback) {
            mpCallback->onHeartRate(mHr, mTrustLevel);
        }
    }
}

bool PAH8316LS::sendCommandToQueue(const Cmd& cmd)
{
    if (mCmdQueue.push(cmd, skQueueTimeout)) {
        return true;
    }

    LOG_ERROR("Failed to send command id = %u\n", cmd.id);
    return false;
}

void PAH8316LS::resetData()
{
    mTouched = false;
    mHr = 0;
    mTrustLevel = 0;
}

void PAH8316LS::resetState()
{
    resetData();
    mPrevPpgState = 0;
}

void PAH8316LS::clearPool()
{
    BodySensorData_t* sample = nullptr;
    while (mAlgoSampleQueue.pop(sample, 0)) {
        osMemoryPoolFree(mMemoryPoolId, sample);
    }
}

void PAH8316LS::platformInit()
{
    static pah_plat plat = {
        .user_data = this,
        .type = PAH_PLAT_COMM_I2C,
        .max_length = 255,
        .write = write,
        .read = read,
        .delay_ms = delayMs,
        .ticks = ticks,
    };

    mId = pah831x_sensor_init(&plat);
}

pah_ret PAH8316LS::write(uint8_t addr, const uint8_t *data, size_t write_size, void *pArgs)
{
    if (pArgs == nullptr || data == nullptr) {
        return PAH_RET_NULL_PTR;
    }

    auto instance = reinterpret_cast<PAH8316LS*>(pArgs);

    if (!instance->writeMem(addr, data, write_size)) {
        return PAH_RET_FAILED;
    }

    return PAH_RET_SUCCESS;
}

pah_ret PAH8316LS::read(uint8_t addr, uint8_t *data, size_t read_size,  void *pArgs)
{
    if (pArgs == nullptr || data == nullptr) {
        return PAH_RET_NULL_PTR;
    }

    auto instance = reinterpret_cast<PAH8316LS*>(pArgs);

    if (!instance->readMem(addr, data, read_size)) {
        return PAH_RET_FAILED;
    }

    return PAH_RET_SUCCESS;
}

void PAH8316LS::delayMs(uint32_t ms, void *pArgs)
{
    (void)pArgs;
    OS::Delay(ms);
}

uint32_t PAH8316LS::ticks(void *pArgs)
{
    (void)pArgs;
    return  OS::GetTimeMs();
}

void PAH8316LS::dataLogStart()
{
    if (mLogToFileEnable) {
        // Create name
        std::tm t {};
        std::time_t utc = time(nullptr);
        localtime_r(&utc, &t); // Takes into account timezone and DST
        char buff[256] {};
        snprintf(buff, sizeof(buff), "%smanufacturer_data_%04u%02u%02u%02u%02u%02u.txt",
                PIXART_LOG_DIR_PATH,
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        mLogToFile.start(buff);
    }
}

void PAH8316LS::dataLogWrite(const char *data, size_t size)
{
    if (data == nullptr || size == 0) {
        return;
    }

    mLogToFile.write(data, size);
}

void PAH8316LS::dataLogStop()
{
    mLogToFile.stop();
}

bool PAH8316LS::writeMem(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!mpI2c || !data) {
        return false;
    }

    bool success = false;
    for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
        if (mpI2c->writeMem(mAddr, static_cast<uint16_t>(addr), 1, data, static_cast<uint16_t>(len))) {
            success = true;
            break;
        }

        if (retry < skI2cRetries) {
            LOG_WARNING("I2C write retry %d\n", retry + 1);
            OS::Delay(2);
        }
    }

    if (!success) {
        LOG_ERROR("Failed to write register 0x%02X after %d retries\n", addr, skI2cRetries);
    }

    return success;
}

bool PAH8316LS::readMem(uint8_t addr, uint8_t *data, size_t len)
{
    if (!mpI2c || !data) {
         return false;
     }

     bool success = false;
     for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
         if (mpI2c->readMem(mAddr, static_cast<uint16_t>(addr), 1, data, static_cast<uint16_t>(len))) {
             success = true;
             break;
         }

         if (retry < skI2cRetries) {
             LOG_WARNING("I2C read retry %d\n", retry + 1);
             OS::Delay(2);
         }
     }

     if (!success) {
         LOG_ERROR("Failed to read register 0x%02X after %d retries\n", addr, skI2cRetries);
     }

     return success;
}

} // Hardware
