/**
 ******************************************************************************
 * @file    ImuBosch.hpp
 * @brief   IMU component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_IMU_BOSCH_HPP
#define COMPONENT_IMU_BOSCH_HPP

#include "IGpio.hpp"
#include "II2c.hpp"
#include "IImu.hpp"
#include "OS.hpp"
#include "Hardware/BMI270/BMI270.hpp"
#include "ImuFifoCadence.hpp"
#include "ImuTimeBase.hpp"

class ImuBosch : public Interface::IImu,
                 public Interface::IExti::Handler,
                 public Hardware::BMI270::IListener
{
public:

    ImuBosch();
    ImuBosch(Interface::II2c& i2c, Interface::IExti& intPin);
    virtual ~ImuBosch();

    bool config(Interface::II2c& i2c, Interface::IExti& intPin);

    void setSensorCallback(SensorCallback *pCallback) override { mSensorCallback = pCallback; }

    bool init() override;
    bool deinit() override;
    bool isInitialized() const override;

    bool resetStepCounter() override;

    /// ImuFifoCadence holds the bounds of what a request comes to.
    bool setFifoLatency(FifoChannel channel, uint32_t ms) override;

    // The rate is asked for as a data period in us. The part runs at the
    // slowest of its rates that is no slower than that, and the accelerometer
    // no slower than BMI270::kFeaturePeriodUs while a feature is enabled.
    // Range: 2,4,8,16 g
    bool enableAccel(uint32_t periodUs) override;
    bool disableAccel() override;
    uint16_t getAccelRange() const override;
    void setAccelRange(uint16_t range) override;
    float getAccelScale() const override;

    // Range: 125,250,500,1000,2000 dps
    bool enableGyro(uint32_t periodUs) override;
    bool disableGyro() override;
    uint16_t getGyroRange() const override;
    void setGyroRange(uint16_t range) override;
    float getGyroScale() const override;

    bool enableStepCounter() override;
    bool disableStepCounter() override;

    bool enableStepDetector() override;
    bool disableStepDetector() override;

    bool enableActivity() override;
    bool disableActivity() override;

    bool enableWristWakeup() override;
    bool disableWristWakeup() override;

    bool enableAnyMotion(bool oneShot = true) override;
    bool disableAnyMotion() override;

    bool enableNoMotion(bool oneShot = true) override;
    bool disableNoMotion() override;

    bool enableSigMotion(bool oneShot = true) override;
    bool disableSigMotion() override;

    bool disableAll() override;

private:

    /// Hardware resources
    Hardware::BMI270 mDrv;
    Interface::IExti* mpIntPin = nullptr;

    /// Indicates that the driver is configured
    bool mIsConfigured = false;

    /// Indicates that the driver is initialized
    enum class InitState { None, InProgress, Initialized };
    std::atomic<InitState> mState{InitState::None};

    std::atomic<uint32_t> mIrqCount{0};

    /// Bounded queue-wait so the worker periodically wakes to run the self-check
    /// even when INT2 is legitimately silent (event-based: a stationary user
    /// produces no interrupts for minutes, so data-silence is NOT a wedge signal).
    static constexpr uint32_t skWatchdogTickMs = 2000;
    /// Consecutive failed liveness reads before a dead chip triggers deep recovery.
    /// A single transient I2C NAK must not reset the chip.
    static constexpr uint32_t skLivenessFailsForReset = 3;
    /// Consecutive deep-recovery attempts before giving up (until the chip
    /// responds again) so a genuinely dead chip does not thrash the worker.
    static constexpr uint32_t skMaxRecoveryAttempts = 3;
    /// Upper bound on the latched-pin drain loop so it can never spin forever.
    static constexpr uint32_t skPinDrainGuard = 16;

    /// Consecutive failed liveness reads observed by the self-check.
    uint32_t mLivenessFails = 0;
    /// Consecutive deep-recovery attempts; reset once the chip is healthy again.
    uint32_t mRecoveryAttempts = 0;

    SensorCallback *mSensorCallback = nullptr;

    Hardware::BMI270::FifoConfig mFifoConfig;

    /// The system time of the FIFO's frames, from the counter value each
    /// carries and the time of the read that brought it.
    ImuTimeBase mTimeBase;

    /// The last time given out in each stream: a stream's times only go up,
    /// for as long as the system clock the reads are dated by does.
    uint64_t mLastAccelUs = 0;
    uint64_t mLastGyroUs  = 0;
    uint64_t mLastSyncUs  = 0;
    uint64_t mLastReadUs  = 0;

    /// The batch in hand, as it goes to the callback.
    SensorRawData  mRawBatch[Hardware::BMI270::kFifoMaxSamples] {};
    SyncSampleData mSyncBatch[Hardware::BMI270::kFifoMaxSamples] {};

    /// What each stream asks for, ms; 0 for no request. Cleared by init(),
    /// and the worker thread's alone once it runs.
    uint32_t mAccelLatencyMs = 0;
    uint32_t mGyroLatencyMs  = 0;

    // Accel Range: 2,4,8,16 g
    Hardware::BMI270::AccelRange mAccelRange = Hardware::BMI270::AccelRange::RANGE_2G;

    // Gyro Range: 125,250,500,1000,2000 dps
    Hardware::BMI270::GyroRange mGyroRange = Hardware::BMI270::GyroRange::RANGE_125DPS;

    bool mNoMotionOneShot = false;
    bool mAnyMotionOneShot = false;
    bool mSigMotionOneShot = false;

    struct Cmd {
        enum class Id : uint8_t {
            NONE = 0,
            DATA_RDY,
            REQUEST_SENSOR,
            RELEASE_SENSOR,
            RELEASE_ALL,
            RESET_STEP_COUNTER,
            SET_FIFO_LATENCY,
        };
        union {
            struct {
                Hardware::BMI270::SensorType sensor;
                Hardware::BMI270::SensorConfig config;
                bool oneShot; // Used for motion only
            } requestSensor;

            struct {
                Hardware::BMI270::SensorType sensor;
            } releaseSensor;

            struct {
                FifoChannel channel;
                uint32_t    latencyMs;
            } fifoLatency;

        } data;
        Id id;
    };

    OS::Queue<Cmd, 50> mCmdQueue;
    OS::Thread<ImuBosch, 2*2048> mThread;

    void thread();

    /// One-time chip startup configuration (INT2, motion thresholds, wrist,
    /// listener). Shared by thread() startup and deep recovery so they can't drift.
    void applyStartupConfig();
    /// Periodic self-check run on a bounded-pop timeout: latched-pin drain plus
    /// liveness detection. NO time-based data-silence detection (event-based INT).
    void selfCheck();
    /// Deep recovery for a dead chip: soft reset + startup config + re-enable
    /// every previously-enabled sub-sensor + re-arm the EXTI.
    bool recoverChip();

    bool sendToQueue(const Cmd& cmd, uint32_t timeout = 100);
    void enableIRQ();
    void disableIRQ(bool force = false);
    virtual void extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge) override;

    void hDataReady(const Cmd &cmd);
    /// Returns true if the sensor went from disabled to enabled (state change).
    /// Caller uses this to gate enableIRQ() so the IRQ refcount tracks actual
    /// sensor count instead of incoming-command count.
    bool hRequestSensor(const Cmd &cmd);
    /// Returns true if the sensor was actually disabled (was enabled before).
    /// Caller uses this to gate disableIRQ() so a release of an already-disabled
    /// sensor does not erroneously decrement the IRQ refcount.
    bool hReleaseSensor(const Cmd &cmd);
    void hReleaseAllSensors(const Cmd &cmd);
    void hResetStepCounter(const Cmd &cmd);
    void hSetFifoLatency(const Cmd &cmd);

    /// FIFO watermark for the streams enabled now and what they ask for.
    uint16_t fifoWatermark() const;

    /// Move the watermark to where it belongs now, if that is elsewhere.
    void refreshFifoWatermark();

    /// Tell the time base whether the gyroscope runs: the part's counter
    /// changes its rate with it.
    void noteGyroState();

    /// Take a batch of a FIFO read into the time base, before its frames
    /// are given their times.
    void takeInBatch(uint32_t newestSensorTime, uint64_t readTimeUs);

    /// The system time of a frame of a read, later than the last given out
    /// in its stream.
    uint64_t frameTime(uint32_t sensorTime, uint64_t readTimeUs, uint64_t& lastUs) const;

    // Callbacks
    virtual void onStepCounter(const Hardware::BMI270::StepData& data) override;
    virtual void onStepDetector(uint64_t timestamp) override;
    virtual void onActivity(const Hardware::BMI270::ActivityData& data) override;
    virtual void onNoMotion(uint64_t timestamp) override;
    virtual void onMotion(uint64_t timestamp) override;
    virtual void onSigMotion(uint64_t timestamp) override;
    virtual void onWristWakeup(uint64_t timestamp) override;
    virtual void onFifoAccel(const Hardware::BMI270::SensorRawData* data, uint16_t count, float scale,
                             uint64_t readTimeUs) override;
    virtual void onFifoGyro(const Hardware::BMI270::SensorRawData* data, uint16_t count, float scale,
                            uint64_t readTimeUs) override;
    virtual void onFifoSync(const Hardware::BMI270::SyncSampleData* data, uint16_t count, float accelScale,
                            float gyroScale, uint64_t readTimeUs) override;
};

#endif // COMPONENT_IMU_BOSCH_HPP
