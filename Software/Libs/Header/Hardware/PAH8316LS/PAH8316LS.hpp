/**
 ******************************************************************************
 * @file    PAH8316LS.hpp
 * @brief   Body sensor Driver with Built-In Library.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef UNA_WATCH_PAH8316LS_HPP
#define UNA_WATCH_PAH8316LS_HPP


#include "IBodySensor.hpp"

#include "II2c.hpp"
#include "IGpio.hpp"
#include "OS.hpp"
#include "Sink/PushFileWriter.hpp"
#include "PixArtHrSession.hpp"

extern "C" {
#include "pah_plat.h"
#include "pah831x_main.h"
}

#include "cmsis_os.h"
#include "freertos_mpool.h"

#include <atomic>

namespace Hardware {

class PAH8316LS : virtual public Interface::IExti::Handler,
                  virtual public Interface::IBodySensor,
                  private PixArtHrSession::DataLog
{
public:

    enum class OperateMode : uint8_t {
      Idle = 0,
      Auto,
      AlwaysRun,
      TouchOnly
    };

    /// Default I2C address
    static const uint8_t skI2cAddr = 0x40;

    /**
     * @brief Constructor.
     */
    PAH8316LS() = default;

    /**
     * @brief Constructor with I2C configuration.
     * @param i2c: Reference to the I2C object.
     * @param intPin: Interrupt pin.
     * @param sensEn: Sensor power enable pin.
     * @param ledEn: LEDs power enable pin
     * @param devAddr: Target device address. 7 bit, not shifted.
     */
    PAH8316LS(Interface::II2c &i2c,
              Interface::IExti &intPin,
              Interface::IGpo &sensEn,
              Interface::IGpo &ledEn,
              uint8_t devAddr = skI2cAddr)
    {
        config(i2c, intPin, sensEn, ledEn, devAddr);
    }

    /**
     * @brief Destructor.
     */
    ~PAH8316LS() {
        deinit();
    }


    /**
     * @brief Configure chip with I2C driver.
     * @param i2c: Pointer to the I2C object.
     * @param intPin: Interrupt pin.
     * @param sensEn: Sensor power enable pin.
     * @param ledEn: LEDs power enable pin
     * @param devAddr: Target device address. 7 bit, not shifted.
     * @return Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c &i2c,
                Interface::IExti &intPin,
                Interface::IGpo &sensEn,
                Interface::IGpo &ledEn,
                uint8_t devAddr = skI2cAddr);

    bool init();
    bool deinit();
    bool isInitialized() const;

    bool enable(OperateMode mode = OperateMode::Auto);

    bool disable();


    bool isTouched();

    void getHeartrate(uint8_t &hr, uint8_t &trustLevel);

    uint16_t getId();

    // Range: 2,4,8,16 g
    void setAccRange(uint8_t range);


    virtual void registerCallback(Interface::IBodySensor::Callback *pCallback) override {
        mpCallback = pCallback;
    }

    virtual void registerAccReqCallback(Interface::IBodySensor::AccRequestCallback *pCallback) override {
        mpAccRequestCallback = pCallback;
    }

    void enableDataLogging(bool enable);

private:
    /// Indicates that the chip is configured
    bool mIsConfigured = false;

    /// I2C address
    uint8_t mAddr = skI2cAddr;

    /// I2C interface pointer
    Interface::II2c *mpI2c = nullptr;

    /// Gpio interface pointers
    Interface::IExti *mpIntPin = nullptr;
    Interface::IGpo *mpSensEn = nullptr;
    Interface::IGpo *mpLedEn = nullptr;

    static constexpr uint32_t skI2cRetries = 3;
    static constexpr uint16_t skSamplePeriod = 50;
    /// Samples per batch: one second at the HR profile's 20 Hz
    static constexpr uint16_t skPpgWatermark = 20;
    static constexpr uint32_t skQueueTimeout = 10;

    /// Bounded queue-wait: the worker wakes at least this often (even with no
    /// command) to run the data-ready staleness watchdog.
    static constexpr uint32_t skWatchdogTickMs = 1000;
    /// No DATA_RDY for this long while streaming means the measurement engine
    /// is wedged. The chip samples every skSamplePeriod ms whenever started, so
    /// a multi-second silence is unambiguous (time-based detection is valid here).
    static constexpr uint32_t skStaleThresholdMs = 3000;
    /// Sensor-VDD off time during a recovery power-cycle. Covers rail bleed plus
    /// the datasheet t6 (>=10 ms) power-on-reset minimum.
    static constexpr uint32_t skMinOffMs = 20;
    /// Consecutive recovery power-cycles before giving up (until next fresh START)
    /// so a genuinely dead chip does not thrash the worker thread.
    static constexpr uint32_t skMaxRecoveryAttempts = 3;
    /// When DATA_RDY is legitimately silent (Auto-untouched / TouchOnly) the chip
    /// is probed via the touch-value heartbeat instead. Recover only after this
    /// many consecutive FAILED heartbeat reads (a chip NAKing the bus); a single
    /// transient NAK must not trigger a power-cycle.
    static constexpr uint32_t skLivenessFailsForReset = 3;
    /// Touch-value heartbeat freeze window. Far longer than the ~10 Hz touch
    /// report period so healthy jitter never trips it; a wedged engine stays
    /// frozen indefinitely, so a generous window costs nothing. On-device the
    /// untouched touch value keeps changing even in darkness, so a sustained
    /// freeze is a reliable wedge signal.
    static constexpr uint32_t skHeartbeatFrozenMs = 10000;

    static constexpr uint32_t skPpgDataLength = 300;
    static constexpr uint32_t skPpgDacDataLength = 300;
    static constexpr uint32_t skAccDataLength = 600;

    /// Indicates that the driver is initialized
    enum class InitState { None, InProgress, Initialized };
    std::atomic<InitState> mState{InitState::None};


    uint8_t mAccRange = 2;  // Range: 2,4,8,16 g
    uint16_t mId = 0;

    bool mTouched = false;
    uint8_t mHr = 0;
    uint8_t mTrustLevel = 0;
    bool mPrevPpgState = false;

    /// Operate mode of the last start(); reused by the recovery power-cycle.
    OperateMode mMode = OperateMode::Auto;
    /// True between a successful start() and stop(); a session is active.
    bool mStreaming = false;
    /// OS tick (ms) of the last processed DATA_RDY; compared to skStaleThresholdMs.
    uint32_t mLastDataReadyTick = 0;
    /// Consecutive watchdog recovery attempts; reset on success or fresh start().
    uint32_t mRecoveryAttempts = 0;
    /// Consecutive failed touch-value heartbeat reads (chip NAKing the bus).
    uint32_t mLivenessFails = 0;
    /// Touch-value heartbeat state. While DATA_RDY is legitimately silent the
    /// touch detector still rewrites the touch raw value every report period
    /// (~10 Hz), regardless of contact and independently of the DATA_RDY
    /// interrupt, so an ADVANCING value proves the measurement engine is alive
    /// and a value frozen for skHeartbeatFrozenMs is the "Calculating..." wedge.
    /// mTouchHbValid is false until a read seeds a baseline (re-invalidated on
    /// start(), on recovery, and whenever fresh DATA_RDY makes the poll moot).
    uint16_t mLastTouchHb = 0;
    uint32_t mLastTouchHbChangeTick = 0;
    bool mTouchHbValid = false;


    ppg_data_out _ppg_data_out_alg {};

    typedef struct {
        uint32_t startTimestamp;
        uint32_t endTimestamp;
        uint32_t timePeriod;
        uint32_t ppgData[skPpgDataLength];
        uint8_t dacData[skPpgDacDataLength];
        uint32_t ppgLength;
        uint8_t channelNum;
        int16_t accData[skAccDataLength];
        uint32_t accLength;
        uint8_t isTouched;
        uint8_t logDataEn;
    } BodySensorData_t;

    static constexpr uint32_t skAlgoSampleOffset = 3;

    osMemoryPoolId_t mMemoryPoolId {nullptr};
    StaticMemPool_t mMemoryPoolCb;
    BodySensorData_t storage[skAlgoSampleOffset];
    OS::Queue<BodySensorData_t*, skAlgoSampleOffset> mAlgoSampleQueue;

    Interface::IBodySensor::Callback *mpCallback = nullptr;
    Interface::IBodySensor::AccRequestCallback *mpAccRequestCallback = nullptr;

    struct Cmd {
        enum class Id : uint8_t {
            NONE = 0,
            DATA_RDY,
            START,
            STOP,
        };

        Id id;
        union {
            OperateMode mode;
        };
    };

    OS::Queue<Cmd, 5> mCmdQueue;
    OS::Thread<PAH8316LS, 2*2048> mThread;

    void thread();
    virtual void extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge) override;

    void start(OperateMode mode);
    void stop();
    /// Power-up + init sequence shared by start() and recover().
    void powerUp(OperateMode mode);
    /// Data-ready staleness watchdog; run on a bounded-pop timeout.
    void watchdogCheck();
    /// Sustained VDD power-cycle + re-init; verifies the chip id came back.
    bool recover();
    void processAlgo();
    bool sendCommandToQueue(const Cmd& cmd);
    void resetData();
    void resetState();
    void clearPool();

    void platformInit();
    static pah_ret write(uint8_t addr, const uint8_t *data, size_t write_size, void *pArgs);
    static pah_ret read(uint8_t addr, uint8_t *data, size_t read_size,  void *pArgs);
    static void delayMs(uint32_t ms, void *pArgs);
    static uint32_t ticks(void *pArgs);

    // PixArtHrSession::DataLog
    void dataLogStart() override;
    void dataLogWrite(const char *data, size_t size) override;
    void dataLogStop() override;

    bool writeMem(uint8_t addr, const uint8_t *data, size_t len);
    bool readMem(uint8_t addr, uint8_t *data, size_t len);


    bool mLogToFileEnable = true;
    Sink::PushFileWriter mLogToFile;

    PixArtHrSession mHrSession {*this};

};

} // Hardware

#endif //UNA_WATCH_PAH8316LS_HPP
