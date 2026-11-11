/**
 ******************************************************************************
 * @file    ImuBosch.cpp
 * @brief   IMU component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ImuBosch.hpp"

#define LOG_MODULE_PRX      "ImuBosch"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <algorithm>

#include "SettingsDebugManager.hpp"

static_assert(ImuTimeBase::skCounterMask == Hardware::Bmi270Fifo::kCounterMask,
              "the time base and the driver disagree on the width of the part's counter");

// An activity goes out by the driver's number for it.
static_assert(static_cast<uint8_t>(Interface::IImu::ActivityType::STILL)
                      == static_cast<uint8_t>(Hardware::BMI270::ActivityType::STILL)
              && static_cast<uint8_t>(Interface::IImu::ActivityType::WALKING)
                      == static_cast<uint8_t>(Hardware::BMI270::ActivityType::WALKING)
              && static_cast<uint8_t>(Interface::IImu::ActivityType::RUNNING)
                      == static_cast<uint8_t>(Hardware::BMI270::ActivityType::RUNNING)
              && static_cast<uint8_t>(Interface::IImu::ActivityType::UNKNOWN)
                      == static_cast<uint8_t>(Hardware::BMI270::ActivityType::UNKNOWN),
              "the interface and the driver number the activities differently");

ImuBosch::ImuBosch()
{

}

ImuBosch::ImuBosch(Interface::II2c &i2c, Interface::IExti &intPin)
{
    config(i2c, intPin);
}

ImuBosch::~ImuBosch()
{
    if(isInitialized()) {
        deinit();
    }
}

bool ImuBosch::config(Interface::II2c &i2c, Interface::IExti &intPin)
{
    mDrv.configure(i2c);
    mpIntPin = &intPin;
    mIsConfigured = true;
    return true;
}

bool ImuBosch::init()
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

    // A request belongs to the run it was made in.
    mAccelLatencyMs = 0;
    mGyroLatencyMs  = 0;

    mCmdQueue.init("ImuBosch");

    mpIntPin->init(Interface::IGpi::Pull::NO_PULL, Interface::IExti::Edge::RISING, 5);
    mpIntPin->setExtiHandler(this);

    if (!mThread.init("ImuBosch", this, &ImuBosch::thread, osPriorityNormal, 5000)) {
        LOG_ERROR("mThread.init timeout\n");
        mpIntPin->deinit();
        mCmdQueue.deinit();
        mState.store(InitState::None);
        return false;
    }

    mState.store(InitState::Initialized);
    LOG_DEBUG("Done\n");
    return true;
}

bool ImuBosch::deinit()
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

    mpIntPin->enableInt(false);
    mIrqCount.store(0, std::memory_order_release);
    mpIntPin->setExtiHandler(nullptr);
    mpIntPin->deinit();

    mCmdQueue.deinit();

    LOG_DEBUG("Done\n");
    return true;
}


bool ImuBosch::isInitialized() const
{
    return mState.load() == InitState::Initialized;
}

bool ImuBosch::resetStepCounter()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RESET_STEP_COUNTER;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::setFifoLatency(FifoChannel channel, uint32_t ms)
{
    Cmd cmd {};
    cmd.id = Cmd::Id::SET_FIFO_LATENCY;
    cmd.data.fifoLatency.channel   = channel;
    cmd.data.fifoLatency.latencyMs = ms;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableAccel(uint32_t periodUs)
{
    if (periodUs == 0) {
        return false;
    }

    // Performance mode above 200 Hz
    Hardware::BMI270::SensorConfig accelConfig = {
        .periodUs = periodUs,
        .range = static_cast<uint16_t>(mAccelRange),
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = periodUs < 5000,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::ACCELEROMETER;
    cmd.data.requestSensor.config = accelConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableAccel()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::ACCELEROMETER;
    return sendToQueue(cmd, 100);
}

uint16_t ImuBosch::getAccelRange() const
{
    return static_cast<uint16_t>(mAccelRange);
}

void ImuBosch::setAccelRange(uint16_t range)
{
    if (range <= 2) {
        mAccelRange = Hardware::BMI270::AccelRange::RANGE_2G;
    } else if (range <= 4) {
        mAccelRange = Hardware::BMI270::AccelRange::RANGE_4G;
    } else if (range <= 8) {
        mAccelRange = Hardware::BMI270::AccelRange::RANGE_8G;
    } else {
        mAccelRange = Hardware::BMI270::AccelRange::RANGE_16G;
    }
}

float ImuBosch::getAccelScale() const
{
    return mDrv.getAccelScale(mAccelRange);
}

bool ImuBosch::enableGyro(uint32_t periodUs)
{
    if (periodUs == 0) {
        return false;
    }

    // Gyroscope requires performance mode for ODR > 100Hz
    // In low-power mode, only 25Hz, 50Hz, 100Hz are supported
    Hardware::BMI270::SensorConfig gyroConfig = {
        .periodUs = periodUs,
        .range = static_cast<uint16_t>(mGyroRange),
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = periodUs < 10000,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::GYROSCOPE;
    cmd.data.requestSensor.config = gyroConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableGyro()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::GYROSCOPE;
    return sendToQueue(cmd, 100);
}

uint16_t ImuBosch::getGyroRange() const
{
    return static_cast<uint16_t>(mGyroRange);
}

void ImuBosch::setGyroRange(uint16_t range)
{
    if (range <= 125) {
        mGyroRange = Hardware::BMI270::GyroRange::RANGE_125DPS;
    } else if (range <= 250) {
        mGyroRange = Hardware::BMI270::GyroRange::RANGE_250DPS;
    } else if (range <= 500) {
        mGyroRange = Hardware::BMI270::GyroRange::RANGE_500DPS;
    } else if (range <= 1000) {
        mGyroRange = Hardware::BMI270::GyroRange::RANGE_1000DPS;
    } else {
        mGyroRange = Hardware::BMI270::GyroRange::RANGE_2000DPS;
    }
}

float ImuBosch::getGyroScale() const
{
    return mDrv.getGyroScale(mGyroRange);
}

bool ImuBosch::enableStepCounter()
{
    Hardware::BMI270::SensorConfig stepConfig = {
          .periodUs = 0,             // Not used for step counter
          .range = 0,                // Not used
          .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
          .performanceMode = false,
          .watermarkLevel = 1        // Interrupt every 1*20 = 20 steps
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::STEP_COUNTER;
    cmd.data.requestSensor.config = stepConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableStepCounter()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::STEP_COUNTER;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableStepDetector()
{
    // Config not really used for detector
    Hardware::BMI270::SensorConfig stepConfig {};

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::STEP_DETECTOR;
    cmd.data.requestSensor.config = stepConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableStepDetector()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::STEP_DETECTOR;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableActivity()
{
    // Activity config (uses accel internally)
    Hardware::BMI270::SensorConfig activityConfig = {
        .periodUs = 0,             // Not used
        .range = 0,                // Not used
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = false,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::ACTIVITY_RECOGNITION;
    cmd.data.requestSensor.config = activityConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableActivity()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::ACTIVITY_RECOGNITION;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableWristWakeup()
{
    Hardware::BMI270::SensorConfig wristConfig = {
        .periodUs = 0,
        .range = 0,
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = false,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::WRIST_WAKEUP;
    cmd.data.requestSensor.config = wristConfig;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableWristWakeup()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::WRIST_WAKEUP;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableAnyMotion(bool oneShot)
{
    // Motion config
    Hardware::BMI270::SensorConfig motionConfig = {
        .periodUs = 0,
        .range = 0,
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = false,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::ANY_MOTION;
    cmd.data.requestSensor.config = motionConfig;
    cmd.data.requestSensor.oneShot = oneShot;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableAnyMotion()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::ANY_MOTION;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableNoMotion(bool oneShot)
{
    // Motion config
    Hardware::BMI270::SensorConfig motionConfig = {
        .periodUs = 0,
        .range = 0,
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = false,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::NO_MOTION;
    cmd.data.requestSensor.config = motionConfig;
    cmd.data.requestSensor.oneShot = oneShot;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableNoMotion()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::NO_MOTION;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::enableSigMotion(bool oneShot)
{
    // Motion config
    Hardware::BMI270::SensorConfig motionConfig = {
        .periodUs = 0,
        .range = 0,
        .bandwidth = Hardware::BMI270::Bandwidth::NORM_AVG4,
        .performanceMode = false,
        .watermarkLevel = 0
    };

    Cmd cmd {};
    cmd.id = Cmd::Id::REQUEST_SENSOR;
    cmd.data.requestSensor.sensor = Hardware::BMI270::SensorType::SIG_MOTION;
    cmd.data.requestSensor.config = motionConfig;
    cmd.data.requestSensor.oneShot = oneShot;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableSigMotion()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_SENSOR;
    cmd.data.releaseSensor.sensor = Hardware::BMI270::SensorType::SIG_MOTION;
    return sendToQueue(cmd, 100);
}

bool ImuBosch::disableAll()
{
    Cmd cmd {};
    cmd.id = Cmd::Id::RELEASE_ALL;
    return sendToQueue(cmd, 100);
}

void ImuBosch::thread()
{
    if (!mDrv.isReady()) {
        mThread.setInitFailed();
        return;
    }

    LOG_INFO("Initialized\n");

    // Initialization successful
    mThread.setInitialized();

    // Apply the one-time chip startup configuration (shared with recovery).
    applyStartupConfig();


    // Register Queue for automatic interruption
    OS::CancellableGuard guard(&mThread, &mCmdQueue);

    // Main loop
    while (!mThread.shouldStop()) {

        // Bounded wait so the worker periodically wakes to run the self-check,
        // even when INT2 is legitimately silent (event-based). On a timeout
        // (no command) run the self-check; real commands are handled below.
        Cmd cmd {};
        if (!mCmdQueue.pop(cmd, skWatchdogTickMs)) {
            selfCheck();
            continue;
        }

        if (mThread.shouldStop()) {
            LOG_DEBUG("Interrupted\n");
            break;
        }

        LOG_DEBUG("cmd id=%u\n", cmd.id);

        switch (cmd.id) {
            case Cmd::Id::DATA_RDY:
                hDataReady(cmd);
                while (mpIntPin->get()) {
                    LOG_DEBUG("repeat\n");
                    hDataReady(cmd);
                }
                break;

            case Cmd::Id::REQUEST_SENSOR:
                // Increment IRQ refcount only when the sensor actually
                // transitioned from disabled to enabled. Otherwise (already
                // enabled -- config update path) the refcount would drift.
                if (hRequestSensor(cmd)) {
                    enableIRQ();
                }
                break;

            case Cmd::Id::RELEASE_SENSOR:
                // Symmetric: decrement only when an actual release happened.
                // Releases of already-disabled sensors are no-ops and must not
                // touch the refcount; otherwise repeated stop/start cycles can
                // drop EXTI while sensors are still active on chip.
                if (hReleaseSensor(cmd)) {
                    disableIRQ();
                }
                break;

            case Cmd::Id::RELEASE_ALL:
                disableIRQ(true);
                hReleaseAllSensors(cmd);
                break;

            case Cmd::Id::RESET_STEP_COUNTER:
                hResetStepCounter(cmd);
                break;

            case Cmd::Id::SET_FIFO_LATENCY:
                hSetFifoLatency(cmd);
                break;

            case Cmd::Id::NONE:
            default:
                break;
        }

    }

    hReleaseAllSensors({});

    mDrv.setListener(nullptr);

    hReleaseAllSensors({});
    disableIRQ(true);

    mCmdQueue.clear();
    LOG_INFO("Deinitialized\n");
}

void ImuBosch::applyStartupConfig()
{
    // Configure interrupt pin
    Hardware::BMI270::InterruptConfig intConfig = {
        .outputEnabled = true,
        .activeHigh = true,
        .pushPull = true,
        .latched = true
    };
    mDrv.setInterruptConfig(Hardware::BMI270::InterruptPin::INT2, intConfig);

    // Initial FIFO config
    mFifoConfig.accelEnabled = false;
    mFifoConfig.gyroEnabled = false;
    mFifoConfig.headerEnabled = true;
    mFifoConfig.sensorTimeEnabled = true;
    mFifoConfig.watermarkBytes = 0;
    mFifoConfig.interruptPin = Hardware::BMI270::InterruptPin::INT2;

    // Configure thresholds
    Hardware::BMI270::MotionConfig anyMotionConfig {};
    anyMotionConfig.duration = 20;  // 20 samples @50Hz = 400ms
    anyMotionConfig.threshold = 83; // 83mg - default
    mDrv.configureAnyMotion(anyMotionConfig);

    Hardware::BMI270::MotionConfig noMotionConfig {};
    noMotionConfig.duration = 150;  // 150 samples @50Hz = 3s
    noMotionConfig.threshold = 83;  // 83mg - default
    mDrv.configureNoMotion(noMotionConfig);

    // Initialize of the wrist configuration
    Hardware::BMI270::WristMotionConfig wristWakeupConfig {};
    wristWakeupConfig.min_angle_focus    = 1774;    // increased to max
    wristWakeupConfig.min_angle_nonfocus = 1774;
    wristWakeupConfig.max_tilt_lr        = 1024;
    wristWakeupConfig.max_tilt_ll        = 700;
    wristWakeupConfig.max_tilt_pd        = 179;
    wristWakeupConfig.max_tilt_pu        = 1978;    // increased to max

    // Overwrite config with user settings
    WatchSettingsDebug debugSettings = Settings::DebugManager::GetInstance().getSettings();
    if (debugSettings.wristCfg.enable) {
        wristWakeupConfig.min_angle_focus    = debugSettings.wristCfg.minAngleFocus;
        wristWakeupConfig.min_angle_nonfocus = debugSettings.wristCfg.minAngleNonfocus;
        wristWakeupConfig.max_tilt_lr        = debugSettings.wristCfg.maxTiltLr;
        wristWakeupConfig.max_tilt_ll        = debugSettings.wristCfg.maxTiltLl;
        wristWakeupConfig.max_tilt_pd        = debugSettings.wristCfg.maxTiltPd;
        wristWakeupConfig.max_tilt_pu        = debugSettings.wristCfg.maxTiltPu;
    }

    mDrv.configureWristWakeup(wristWakeupConfig);

    mDrv.setListener(this);
}

void ImuBosch::selfCheck()
{
    // Only meaningful while at least one sub-sensor is enabled on the chip.
    // NOTE: absence of interrupts is NOT a wedge signal here -- BMI270 step/
    // motion interrupts are event-based, so a stationary user is silent for
    // minutes legitimately. We only act on a latched pin or a dead bus.
    if (mDrv.getEnabledSensorsMask() == 0) {
        return;
    }

    // (a) Latched-pin recovery (safe, no false positives): an INT2 event may
    // have latched high but its DATA_RDY notification was lost. Draining it
    // (reading INT_STATUS inside processInterrupt) clears the latch. A stationary
    // user has the pin LOW, so this no-ops. The loop is bounded so it can never
    // spin forever on a stuck-high pin.
    if (mpIntPin->get()) {
        Cmd cmd {};
        cmd.id = Cmd::Id::DATA_RDY;
        uint32_t guard = 0;
        do {
            hDataReady(cmd);
        } while (mpIntPin->get() && ++guard < skPinDrainGuard);
        mLivenessFails = 0;  // chip is clearly alive and delivering
        return;
    }

    // (b) Liveness / dead-chip detection: a lightweight bus read. Only sustained
    // failures escalate to a deep reset; a single transient NAK must not.
    if (mDrv.isReady()) {
        mLivenessFails = 0;
        mRecoveryAttempts = 0;  // healthy: restore the recovery budget
        return;
    }

    if (++mLivenessFails < skLivenessFailsForReset) {
        LOG_WARNING("IMU liveness read failed (%lu/%lu)\n",
                    (unsigned long)mLivenessFails,
                    (unsigned long)skLivenessFailsForReset);
        return;
    }

    if (mRecoveryAttempts >= skMaxRecoveryAttempts) {
        return;  // give up until the chip responds again
    }

    mRecoveryAttempts++;
    LOG_ERROR("IMU appears dead; deep recovery attempt %lu/%lu\n",
              (unsigned long)mRecoveryAttempts,
              (unsigned long)skMaxRecoveryAttempts);

    if (recoverChip()) {
        LOG_INFO("IMU recovery succeeded\n");
        mLivenessFails = 0;
        mRecoveryAttempts = 0;
    }
}

bool ImuBosch::recoverChip()
{
    constexpr uint8_t kMaxSensors = static_cast<uint8_t>(Hardware::BMI270::SensorType::MAX_SENSORS);

    // Snapshot the currently-enabled sensors and their configs BEFORE the soft
    // reset -- softReset() clears the driver's enabled flags.
    const uint16_t mask = mDrv.getEnabledSensorsMask();
    Hardware::BMI270::SensorConfig configs[kMaxSensors];
    for (uint8_t i = 0; i < kMaxSensors; i++) {
        if (mask & (1u << i)) {
            configs[i] = mDrv.getSensorConfig(static_cast<Hardware::BMI270::SensorType>(i));
        }
    }

    // The reset starts the part's counter anew, whether or not the part
    // answers after it.
    mTimeBase.reset();

    // Deep reset + replay the one-time startup configuration.
    const bool wasReset = mDrv.softReset();

    noteGyroState();

    if (!wasReset) {
        LOG_ERROR("softReset failed during recovery\n");
        return false;
    }

    // The part holds no stream now, whether or not it answers: the FIFO is
    // set up from nothing by whatever is requested next.
    mFifoConfig.accelEnabled = false;
    mFifoConfig.gyroEnabled  = false;

    if (!mDrv.isReady()) {
        LOG_ERROR("IMU still not responding after soft reset\n");
        return false;
    }

    applyStartupConfig();

    // Re-enable every previously-enabled sub-sensor so mapping/FIFO/thresholds
    // are restored (including STEP_COUNTER, so its epoch continuity holds). The
    // IRQ refcount already reflects the intended sensor count and was never
    // released during the wedge, so re-arm the EXTI directly below instead of
    // going through enableIRQ() (which would double-count the refcount).
    for (uint8_t i = 0; i < kMaxSensors; i++) {
        if (!(mask & (1u << i))) {
            continue;
        }

        const auto sensor = static_cast<Hardware::BMI270::SensorType>(i);
        Cmd cmd {};
        cmd.id = Cmd::Id::REQUEST_SENSOR;
        cmd.data.requestSensor.sensor = sensor;
        cmd.data.requestSensor.config = configs[i];

        // Preserve one-shot semantics for the motion features.
        if (sensor == Hardware::BMI270::SensorType::ANY_MOTION) {
            cmd.data.requestSensor.oneShot = mAnyMotionOneShot;
        } else if (sensor == Hardware::BMI270::SensorType::NO_MOTION) {
            cmd.data.requestSensor.oneShot = mNoMotionOneShot;
        } else if (sensor == Hardware::BMI270::SensorType::SIG_MOTION) {
            cmd.data.requestSensor.oneShot = mSigMotionOneShot;
        }

        if (!hRequestSensor(cmd)) {
            LOG_WARNING("Recovery re-enable incomplete for sensor %u\n", (unsigned)i);
        }
    }

    // Re-arm the interrupt (refcount is unchanged by recovery).
    mpIntPin->clearInt();
    mpIntPin->setExtiHandler(this);
    mpIntPin->enableInt(true);

    return true;
}

bool ImuBosch::sendToQueue(const Cmd& cmd, uint32_t timeout)
{
    if (!isInitialized()) {
        LOG_ERROR("Driver not initializes\n");
        return false;
    }

    if (!mCmdQueue.push(cmd, timeout)) {
        LOG_ERROR("Failed to send cmd id=%u to the queue. timeout %u ms\n", cmd.id, timeout);
        return false;
    }

    return true;
}

void ImuBosch::enableIRQ()
{
    uint32_t prev = mIrqCount.fetch_add(1, std::memory_order_acq_rel);

    if (prev == 0) {
        mpIntPin->setExtiHandler(this);
        mpIntPin->enableInt(true);

        // An event may have latched INT2 high while the EXTI was disabled.
        // Re-check the pin so that edge is not lost: kick one processing pass
        // (reading INT_STATUS inside processInterrupt() clears the latch). We are
        // already on the worker thread, so this keeps all chip access on-thread.
        if (mpIntPin->get()) {
            Cmd cmd {};
            cmd.id = Cmd::Id::DATA_RDY;
            hDataReady(cmd);
        }
    }
}

void ImuBosch::disableIRQ(bool force)
{
    uint32_t prev = mIrqCount.load(std::memory_order_acquire);

    if (!force) {
        if (prev == 0) {
            return;
        }
        prev = mIrqCount.fetch_sub(1, std::memory_order_acq_rel);
    }

    if (prev == 1 || force) {
        mpIntPin->enableInt(false);
        mpIntPin->setExtiHandler(nullptr);
        mIrqCount.store(0, std::memory_order_release);
    }
}

void ImuBosch::extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge)
{
    if (!isInitialized()) {
        LOG_ERROR("Driver not initializes\n");
        return;
    }
    // Notify thread
    Cmd cmd {};
    cmd.id = Cmd::Id::DATA_RDY;

    if(!mCmdQueue.push(cmd, 0)) {
        LOG_ERROR("Failed to send cmd id=%u to the queue from interrupt\n", cmd.id);
    }
}

void ImuBosch::hDataReady(const Cmd &cmd)
{
    mDrv.processInterrupt();
}

bool ImuBosch::hRequestSensor(const Cmd &cmd)
{
    const auto sensor = cmd.data.requestSensor.sensor;

    //// Snapshot the chip state BEFORE requestSensor() so we can tell whether
    //// this call performs a real enable or just a config update on an already
    //// enabled sensor (which the BMI270 driver treats as success).
    const bool wasEnabled = mDrv.isSensorEnabled(sensor);

    if (!mDrv.requestSensor(sensor, cmd.data.requestSensor.config)) {
        LOG_ERROR("Failed to request sensor %d\n", static_cast<uint8_t>(sensor));
        return false;
    }

    if (sensor == Hardware::BMI270::SensorType::GYROSCOPE) {
        noteGyroState();
    }

    if (sensor == Hardware::BMI270::SensorType::ANY_MOTION) {
        mAnyMotionOneShot = cmd.data.requestSensor.oneShot;
    } else if (sensor == Hardware::BMI270::SensorType::NO_MOTION) {
        mNoMotionOneShot = cmd.data.requestSensor.oneShot;
    } else if (sensor == Hardware::BMI270::SensorType::SIG_MOTION) {
        mSigMotionOneShot = cmd.data.requestSensor.oneShot;
    }

    if (sensor == Hardware::BMI270::SensorType::ACCELEROMETER ||
        sensor == Hardware::BMI270::SensorType::GYROSCOPE)
    {
        if (sensor == Hardware::BMI270::SensorType::ACCELEROMETER) {
            mFifoConfig.accelEnabled = true;
        }
        if (sensor == Hardware::BMI270::SensorType::GYROSCOPE) {
            mFifoConfig.gyroEnabled = true;
        }

        mFifoConfig.watermarkBytes = fifoWatermark();
        if (!mDrv.configureFifo(mFifoConfig)) {
            LOG_ERROR("Failed to configure FIFO for sensor %d\n",
                    static_cast<uint8_t>(sensor));
        } else {
            LOG_DEBUG("FIFO watermark %u bytes\n", mFifoConfig.watermarkBytes);
        }
    } else {
        if (!mDrv.mapSensorInterrupt(sensor, Hardware::BMI270::InterruptPin::INT2)) {
            LOG_ERROR("Failed to map interrupt for sensor %d\n",
                    static_cast<uint8_t>(sensor));
        }

        // A feature can hold the accelerometer to a faster rate than was
        // asked for, so enabling one can change the rate the FIFO fills at.
        refreshFifoWatermark();
    }

    //// Only newly enabled sensors should bump the IRQ refcount upstream.
    return !wasEnabled;
}

bool ImuBosch::hReleaseSensor(const Cmd &cmd)
{
    const auto sensor = cmd.data.releaseSensor.sensor;

    //// Releases of already-disabled sensors are no-ops in BMI270 but they
    //// must not affect the IRQ refcount or FIFO state. Detect that here and
    //// return false so the caller skips disableIRQ().
    if (!mDrv.isSensorEnabled(sensor)) {
        return false;
    }

    mDrv.releaseSensor(sensor);

    if (sensor == Hardware::BMI270::SensorType::GYROSCOPE) {
        noteGyroState();
    }

    // Only the two streams feed the FIFO. Releasing anything else leaves it
    // as it is: the features come and go all the time.
    if (sensor != Hardware::BMI270::SensorType::ACCELEROMETER &&
        sensor != Hardware::BMI270::SensorType::GYROSCOPE)
    {
        // The level is another matter: with its last feature gone the
        // accelerometer may have gone back to a slower rate.
        refreshFifoWatermark();
        return true;
    }

    if (sensor == Hardware::BMI270::SensorType::ACCELEROMETER) {
        mFifoConfig.accelEnabled = false;
    }
    if (sensor == Hardware::BMI270::SensorType::GYROSCOPE) {
        mFifoConfig.gyroEnabled = false;
    }

    if (mFifoConfig.accelEnabled || mFifoConfig.gyroEnabled) {
        mFifoConfig.watermarkBytes = fifoWatermark();
        if (!mDrv.configureFifo(mFifoConfig)) {
            LOG_ERROR("Failed to configure FIFO\n");
        } else {
            LOG_DEBUG("FIFO watermark %u bytes\n", mFifoConfig.watermarkBytes);
        }
    } else {
        mDrv.disableFifo();

        // With no stream to read, nothing ties the part's counter to the
        // system clock until the next one starts.
        mTimeBase.reset();
    }

    return true;
}

void ImuBosch::hReleaseAllSensors(const Cmd &cmd)
{
    (void) cmd;
    mDrv.releaseAll();
    mDrv.disableFifo();

    // Nothing feeds the FIFO now; the next stream sets it up for itself alone.
    mFifoConfig.accelEnabled = false;
    mFifoConfig.gyroEnabled  = false;

    mTimeBase.reset();
    noteGyroState();
}

void ImuBosch::hResetStepCounter(const Cmd &cmd)
{
    mDrv.resetStepCounter();
}

void ImuBosch::hSetFifoLatency(const Cmd &cmd)
{
    if (cmd.data.fifoLatency.channel == FifoChannel::ACCEL) {
        mAccelLatencyMs = cmd.data.fifoLatency.latencyMs;
    } else {
        mGyroLatencyMs = cmd.data.fifoLatency.latencyMs;
    }

    refreshFifoWatermark();
}

void ImuBosch::refreshFifoWatermark()
{
    // With nothing feeding the FIFO there is no watermark to move; enabling
    // a stream sizes it from what is stored here.
    if (!mFifoConfig.accelEnabled && !mFifoConfig.gyroEnabled) {
        return;
    }

    const uint16_t watermark = fifoWatermark();
    if (watermark == mFifoConfig.watermarkBytes) {
        return;
    }

    // Only the level is moved: the streams in the FIFO are as they were.
    if (!mDrv.setFifoWatermark(watermark)) {
        LOG_ERROR("Failed to set FIFO watermark %u\n", watermark);
        return;
    }

    mFifoConfig.watermarkBytes = watermark;
    LOG_DEBUG("FIFO watermark %u bytes\n", watermark);
}

void ImuBosch::noteGyroState()
{
    // On the driver's clock, the one the reads are dated by.
    mTimeBase.setGyroRunning(mDrv.isSensorEnabled(Hardware::BMI270::SensorType::GYROSCOPE), mDrv.getSystemTime());
}

void ImuBosch::takeInBatch(uint32_t newestSensorTime, uint64_t readTimeUs)
{
    // The system clock went back: the times given out before are of a clock
    // that is no more, and nothing new is held behind them.
    if (readTimeUs < mLastReadUs) {
        mLastAccelUs = 0;
        mLastGyroUs  = 0;
        mLastSyncUs  = 0;
    }

    mLastReadUs = readTimeUs;

    mTimeBase.observe(newestSensorTime, readTimeUs);
}

uint64_t ImuBosch::frameTime(uint32_t sensorTime, uint64_t readTimeUs, uint64_t& lastUs) const
{
    uint64_t at = mTimeBase.stamp(sensorTime, readTimeUs);

    // In order comes before not later than the read: where the time base
    // has no room left under the read, the frames go a microsecond apart
    // and may pass it.
    if (at <= lastUs) {
        at = lastUs + 1U;
    }

    lastUs = at;

    return at;
}

uint16_t ImuBosch::fifoWatermark() const
{
    const uint32_t period = ImuFifoCadence::periodMs(mFifoConfig.accelEnabled, mAccelLatencyMs,
                                                     mFifoConfig.gyroEnabled, mGyroLatencyMs);

    return mDrv.calculateFifoWatermark(period, mFifoConfig.headerEnabled, ImuFifoCadence::kReadReserveMs);
}

void ImuBosch::onStepCounter(const Hardware::BMI270::StepData& data)
{
    LOG_DEBUG("Steps: %lu steps, t=%.1f\n", data.steps, data.timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onStepCounter(data.steps, data.timestamp);
    }
}

void ImuBosch::onStepDetector(uint64_t timestamp)
{
    LOG_DEBUG("Step detected, t=%.1f\n", timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onStepDetector(timestamp);
    }
}

void ImuBosch::onActivity(const Hardware::BMI270::ActivityData& data)
{
    const char* activityStr = "Unknown";
    switch (data.activity) {
        case Hardware::BMI270::ActivityType::STILL:   activityStr = "Still"; break;
        case Hardware::BMI270::ActivityType::WALKING: activityStr = "Walking"; break;
        case Hardware::BMI270::ActivityType::RUNNING: activityStr = "Running"; break;
        default: activityStr = "Unknown"; break;
    }

    LOG_DEBUG("Activity: %s (%d), t=%.1f\n", activityStr, data.activity, data.timestamp / 1000.0f);
    (void)activityStr;

    if (mSensorCallback) {
        mSensorCallback->onActivity(static_cast<ActivityType>(data.activity), data.timestamp);
    }
}

void ImuBosch::onNoMotion(uint64_t timestamp)
{
    LOG_DEBUG("No motion, t=%.1f\n", timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onNoMotion(timestamp);
    }

    //// One-shot self-release. This bypasses the cmd queue (we are already
    //// running on the BMI270 thread inside hDataReady), so we must also
    //// balance the IRQ refcount manually -- otherwise it stays high forever.
    //// isSensorEnabled() guards against double-release if the upper layer
    //// also called disableNoMotion() concurrently.
    if (mNoMotionOneShot &&
        mDrv.isSensorEnabled(Hardware::BMI270::SensorType::NO_MOTION))
    {
        mNoMotionOneShot = false;
        mDrv.releaseSensor(Hardware::BMI270::SensorType::NO_MOTION);
        refreshFifoWatermark();
        disableIRQ();
    }
}

void ImuBosch::onMotion(uint64_t timestamp)
{
    LOG_DEBUG("Any motion, t=%.1f\n", timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onMotion(timestamp);
    }

    if (mAnyMotionOneShot &&
        mDrv.isSensorEnabled(Hardware::BMI270::SensorType::ANY_MOTION))
    {
        mAnyMotionOneShot = false;
        mDrv.releaseSensor(Hardware::BMI270::SensorType::ANY_MOTION);
        refreshFifoWatermark();
        disableIRQ();
    }
}

void ImuBosch::onSigMotion(uint64_t timestamp)
{
    LOG_DEBUG("Significant motion, t=%.1f\n", timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onSigMotion(timestamp);
    }

    if (mSigMotionOneShot &&
        mDrv.isSensorEnabled(Hardware::BMI270::SensorType::SIG_MOTION))
    {
        mSigMotionOneShot = false;
        mDrv.releaseSensor(Hardware::BMI270::SensorType::SIG_MOTION);
        refreshFifoWatermark();
        disableIRQ();
    }
}

void ImuBosch::onWristWakeup(uint64_t timestamp)
{
    LOG_DEBUG("Wrist wakeup, t=%.1f\n", timestamp / 1000.0f);

    if (mSensorCallback) {
        mSensorCallback->onWristWakeup(timestamp);
    }
}

void ImuBosch::onFifoAccel(const Hardware::BMI270::SensorRawData* data, uint16_t count, float scale,
                           uint64_t readTimeUs)
{
    count = std::min<uint16_t>(count, Hardware::BMI270::kFifoMaxSamples);

    if (count == 0) {
        return;
    }

    takeInBatch(data[count - 1].sensorTime, readTimeUs);

    for (uint16_t i = 0; i < count; i++) {
        mRawBatch[i].x         = data[i].x;
        mRawBatch[i].y         = data[i].y;
        mRawBatch[i].z         = data[i].z;
        mRawBatch[i].timestamp = frameTime(data[i].sensorTime, readTimeUs, mLastAccelUs);
    }

    LOG_DEBUG("Accel: %d samples, scale %.6f\n", count, scale);
    LOG_DEBUG("  [%3d] t=%.1f x=%d y=%d z=%d\n", 0,
            mRawBatch[0].timestamp / 1000.0f, mRawBatch[0].x, mRawBatch[0].y, mRawBatch[0].z);
    if (count > 1) {
        uint16_t last = count - 1;
        LOG_DEBUG("  [%3d] t=%.1f, Delta: %.1f ms\n", last, mRawBatch[last].timestamp / 1000.0f,
                  (mRawBatch[last].timestamp - mRawBatch[0].timestamp) / 1000.0f);
        (void)last;
    }

    if (mSensorCallback) {
        // Called from inside BMI270's FIFO read, before any rate change that
        // read is part of, so this is the rate these frames were taken at.
        mSensorCallback->onAccelRaw(mRawBatch, count, scale,
                mDrv.getAppliedPeriodUs(Hardware::BMI270::SensorType::ACCELEROMETER));
    }
}

void ImuBosch::onFifoGyro(const Hardware::BMI270::SensorRawData* data, uint16_t count, float scale,
                          uint64_t readTimeUs)
{
    count = std::min<uint16_t>(count, Hardware::BMI270::kFifoMaxSamples);

    if (count == 0) {
        return;
    }

    takeInBatch(data[count - 1].sensorTime, readTimeUs);

    for (uint16_t i = 0; i < count; i++) {
        mRawBatch[i].x         = data[i].x;
        mRawBatch[i].y         = data[i].y;
        mRawBatch[i].z         = data[i].z;
        mRawBatch[i].timestamp = frameTime(data[i].sensorTime, readTimeUs, mLastGyroUs);
    }

    LOG_DEBUG("Gyro: %d samples, scale %.6f\n", count, scale);
    LOG_DEBUG("  [%3d] t=%.1f x=%d y=%d z=%d\n", 0,
            mRawBatch[0].timestamp / 1000.0f, mRawBatch[0].x, mRawBatch[0].y, mRawBatch[0].z);
    if (count > 1) {
        uint16_t last = count - 1;
        LOG_DEBUG("  [%3d] t=%.1f, Delta: %.1f ms\n", last, mRawBatch[last].timestamp / 1000.0f,
                  (mRawBatch[last].timestamp - mRawBatch[0].timestamp) / 1000.0f);
        (void)last;
    }

    if (mSensorCallback) {
        // As for the accelerometer: the rate these frames were taken at.
        mSensorCallback->onGyroRaw(mRawBatch, count, scale,
                mDrv.getAppliedPeriodUs(Hardware::BMI270::SensorType::GYROSCOPE));
    }
}

void ImuBosch::onFifoSync(const Hardware::BMI270::SyncSampleData *data, uint16_t count, float accelScale,
                          float gyroScale, uint64_t readTimeUs)
{
    count = std::min<uint16_t>(count, Hardware::BMI270::kFifoMaxSamples);

    if (count == 0) {
        return;
    }

    takeInBatch(data[count - 1].sensorTime, readTimeUs);

    for (uint16_t i = 0; i < count; i++) {
        mSyncBatch[i].accel_x   = data[i].accel_x;
        mSyncBatch[i].accel_y   = data[i].accel_y;
        mSyncBatch[i].accel_z   = data[i].accel_z;
        mSyncBatch[i].gyro_x    = data[i].gyro_x;
        mSyncBatch[i].gyro_y    = data[i].gyro_y;
        mSyncBatch[i].gyro_z    = data[i].gyro_z;
        mSyncBatch[i].timestamp = frameTime(data[i].sensorTime, readTimeUs, mLastSyncUs);
    }

    LOG_DEBUG("Sync: %d samples, accel scale %.6f, gyro  scale %.6f\n", count, accelScale, gyroScale);
    LOG_DEBUG("  [%3d] t=%.1f ax=%d ay=%d az=%d gx=%d gy=%d gz=%d\n", 0,
            mSyncBatch[0].timestamp / 1000.0f,
            mSyncBatch[0].accel_x, mSyncBatch[0].accel_y, mSyncBatch[0].accel_z,
            mSyncBatch[0].gyro_x, mSyncBatch[0].gyro_y, mSyncBatch[0].gyro_z);
    if (count > 1) {
        uint16_t last = count - 1;
        LOG_DEBUG("  [%3d] t=%.1f, Delta: %.1f ms\n", last, mSyncBatch[last].timestamp / 1000.0f,
                  (mSyncBatch[last].timestamp - mSyncBatch[0].timestamp) / 1000.0f);
        (void)last;
    }

    if (mSensorCallback) {
        // Sync mode needs both streams at one rate, so the accelerometer's
        // step is the gyroscope's too.
        mSensorCallback->onSyncRaw(mSyncBatch, count, accelScale, gyroScale,
                mDrv.getAppliedPeriodUs(Hardware::BMI270::SensorType::ACCELEROMETER));
    }
}
