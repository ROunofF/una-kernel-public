/**
 ******************************************************************************
 * @file    BMI270.hpp
 * @brief   BMI270 IMU driver.
 ******************************************************************************
 *
 * Features:
 * - Reference counting for sensor dependencies
 * - Lazy chip init/deinit
 * - Separated configure/enable phases
 * - Batch operations support
 * - FIFO-centric design
 *
 ******************************************************************************
 */

#ifndef HARDWARE_BMI270_HPP
#define HARDWARE_BMI270_HPP

#include "II2c.hpp"
#include "bmi2_defs.h"
#include "Hardware/BMI270/FifoFrames.hpp"
#include "Hardware/BMI270/FifoGrid.hpp"
#include "Hardware/BMI270/FifoWatermark.hpp"
#include "Hardware/BMI270/Bmi270Odr.hpp"
#include <cstdint>

namespace Hardware
{

class BMI270 {

public:

    /***************************************************************************
     * Constants
     **************************************************************************/

    static constexpr uint8_t kI2cAddr = 0x68;
    static constexpr uint8_t kI2cAddrSecondary = 0x69;
    static constexpr uint8_t kSensorTimeBytes = 3;
    static constexpr uint32_t kI2cRetries = 5;

    /**
     * @brief Size of the buffer a FIFO read goes into.
     *
     * A read is taken to the end of the FIFO, and the FIFO fills while it is
     * read: the buffer holds a full FIFO and what both sensors at their
     * fastest add to it in the time it takes to read out.
     */
    static constexpr uint16_t kFifoBufferSize = 3072;

    /// Reads a FIFO read-out may take to reach the end of the FIFO.
    static constexpr uint8_t kFifoReadRounds = 4;

    /**
     * @brief Maximum number of samples that can fit in FIFO buffer.
     *
     * Calculated as kFifoBufferSize / 6, where 6 bytes is the size of
     * one sensor sample (2 bytes per axis * 3 axes).
     * This represents the theoretical maximum without headers or sensor time.
     * With headers enabled, actual capacity is slightly less.
     */
    static constexpr uint16_t kFifoMaxSamples = kFifoBufferSize / 6;

    /**
     * @brief Maximum watermark value for standard BMI270 variant.
     *
     * Although BMI270 has 6144 bytes of physical FIFO memory, the standard
     * bmi270.c variant limits usable FIFO to approximately 2016 bytes.
     * This limitation occurs because the sensor's internal RAM is shared
     * between FIFO buffer and feature firmware (step counter, gesture
     * detection, etc.).
     *
     * The standard config file (~8KB) loads firmware for all features,
     * consuming ~4KB of internal memory and leaving ~2KB for FIFO.
     * The bmi270_maximum_fifo variant uses minimal firmware (~330 bytes)
     * with no features, allowing full 6144 byte FIFO access.
     *
     * @note This limit is undocumented by Bosch and was determined through
     *       testing. Watermark values above ~2010 bytes cause FIFO watermark
     *       interrupt failures.
     * @note See calculateFifoWatermark() function.
     */
    static constexpr uint16_t kFifoWatermarkLimit = 2000;

    /**
     * @brief Longest accelerometer data period the features work with: 50 Hz.
     *
     * The features run on the accelerometer data. With the accelerometer
     * filter in its power-optimised mode the part asks for a rate of at
     * least 50 Hz and raises odr_50hz_error in INTERNAL_STATUS below it.
     * While any feature is enabled the accelerometer is therefore held to
     * this period or a shorter one, whatever was asked for it.
     */
    static constexpr uint32_t kFeaturePeriodUs = 20000;

    /***************************************************************************
     * Enumerations
     **************************************************************************/

    enum class SensorType : uint8_t {
        ACCELEROMETER        = 0,
        GYROSCOPE            = 1,
        TEMPERATURE          = 2,
        STEP_COUNTER         = 3,
        STEP_DETECTOR        = 4,
        ACTIVITY_RECOGNITION = 5,
        WRIST_WAKEUP         = 6,
        NO_MOTION            = 7,
        ANY_MOTION           = 8,
        SIG_MOTION           = 9,
        WRIST_GESTURE        = 10,
        MAX_SENSORS
    };

    enum class AccelRange : uint8_t {
        RANGE_2G  = 2,
        RANGE_4G  = 4,
        RANGE_8G  = 8,
        RANGE_16G = 16
    };

    enum class GyroRange : uint16_t {
        RANGE_125DPS  = 125,
        RANGE_250DPS  = 250,
        RANGE_500DPS  = 500,
        RANGE_1000DPS = 1000,
        RANGE_2000DPS = 2000
    };

    enum class Bandwidth : uint8_t {
        OSR4_AVG1   = 0,
        OSR2_AVG2   = 1,
        NORM_AVG4   = 2,
        CIC_AVG8    = 3,
        RES_AVG16   = 4,
        RES_AVG32   = 5,
        RES_AVG64   = 6,
        RES_AVG128  = 7
    };

    enum class ODR : uint8_t {
        ODR_0_78_HZ  = 0x01,
        ODR_1_56_HZ  = 0x02,
        ODR_3_12_HZ  = 0x03,
        ODR_6_25_HZ  = 0x04,
        ODR_12_5_HZ  = 0x05,
        ODR_25_HZ    = 0x06,
        ODR_50_HZ    = 0x07,
        ODR_100_HZ   = 0x08,
        ODR_200_HZ   = 0x09,
        ODR_400_HZ   = 0x0A,
        ODR_800_HZ   = 0x0B,
        ODR_1600_HZ  = 0x0C
    };

    enum class ActivityType : uint8_t {
        STILL   = 0,
        WALKING = 1,
        RUNNING = 2,
        UNKNOWN = 3
    };

    enum class WristGestureType : uint8_t {
        UNKNOWN         = 0,  /*!< No gesture detected or feature just enabled */
        PUSH_ARM_DOWN   = 1,  /*!< Arm pushed quickly downward then returned */
        PIVOT_UP        = 2,  /*!< Arm pivoted quickly upward then returned */
        SHAKE_JIGGLE    = 3,  /*!< Wrist shaken or jiggled quickly */
        FLICK_IN        = 4,  /*!< Slow roll-out followed by quick roll-in */
        FLICK_OUT       = 5   /*!< Quick roll-out followed by slow roll-in */
    };

    enum class InterruptPin : uint8_t {
        INT1 = 0,
        INT2 = 1
    };

    /***************************************************************************
     * Data Structures
     **************************************************************************/

    struct SensorData {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint64_t timestamp = 0;  // System time in microseconds
    };

    struct SensorRawData {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;

        /// The part's own 24-bit counter when it took the reading, for one
        /// that came out of the FIFO in header mode with the sensortime
        /// frame on; 0 for any other out of the FIFO. A reading asked for on
        /// its own has the counter as read after it. One count is 39.0625 us.
        /// The counter runs a little off the system clock, wraps at 24 bits,
        /// and in a stream only ever increases; it starts anew when the part
        /// is reset.
        uint32_t sensorTime = 0;
    };

    struct SyncSampleData {
        int16_t accel_x = 0;
        int16_t accel_y = 0;
        int16_t accel_z = 0;
        int16_t gyro_x = 0;
        int16_t gyro_y = 0;
        int16_t gyro_z = 0;
        uint32_t sensorTime = 0; ///< As SensorRawData::sensorTime, one for the pair
    };

    struct StepData {
        uint32_t steps = 0;
        uint64_t timestamp = 0;  // System time in microseconds
    };

    struct ActivityData {
        ActivityType activity = ActivityType::UNKNOWN;
        uint64_t timestamp = 0;  // System time in microseconds
    };

    struct WristGestureData {
        WristGestureType gesture = WristGestureType::UNKNOWN;
        uint64_t timestamp = 0;  // System time in microseconds
    };

    /**
     * @brief Configuration for WRIST_GESTURE detection.
     *
     * All angular parameters are scaled by 2048 (i.e. 2048 * sin/cos(angle)).
     * wearable_arm: 0 = left arm (default), 1 = right arm.
     * min_flick_peak: sine of min tilt angle during flick, range 1448-1774, default 1774.
     * min_flick_samples: min time between roll-out and roll-in at 50 Hz, range 3-5, default 4.
     * max_duration: max time to complete a gesture at 50 Hz, range 150-250, default 200.
     */
    struct WristGestureConfig {
        uint16_t wearable_arm       = 0;     /*!< 0=left arm, 1=right arm */
        uint16_t min_flick_peak     = 1774;  /*!< 2048*sin(angle), range 1448-1774 */
        uint16_t min_flick_samples  = 4;     /*!< samples at 50 Hz, range 3-5 */
        uint16_t max_duration       = 200;   /*!< samples at 50 Hz, range 150-250 */
    };

    struct SensorConfig {
        /// Data period asked for, us. The part is set to the slowest rate
        /// that is no slower than this; see getAppliedPeriodUs() for the
        /// period it then runs at. Not used by the features.
        uint32_t periodUs = 0;
        uint16_t range = 0;           // Acc: 2,4,8,16g; Gyro: 125,250,500,1000,2000 dps
        Bandwidth bandwidth = Bandwidth::NORM_AVG4;
        bool performanceMode = false; // true = performance, false = power optimized
        uint16_t watermarkLevel = 0;  // For step counter: steps = level * 20
    };

    struct MotionConfig {
        uint16_t threshold = 83;      // Threshold in mg (0-1000), default 83mg
        uint16_t duration = 5;        // Duration in samples (0-163), default 5
        bool selectX = true;          // Enable X-axis detection
        bool selectY = true;          // Enable Y-axis detection
        bool selectZ = true;          // Enable Z-axis detection
    };

    struct WristMotionConfig {
        uint16_t min_angle_focus    = 1024;
        uint16_t min_angle_nonfocus = 1774;
        uint16_t max_tilt_lr        = 1024;
        uint16_t max_tilt_ll        = 700;
        uint16_t max_tilt_pd        = 179;
        uint16_t max_tilt_pu        = 1925;
    };

    struct FifoConfig {
        bool accelEnabled = false;
        bool gyroEnabled = false;
        bool auxEnabled = false;
        bool headerEnabled = false;
        bool sensorTimeEnabled = false;
        uint16_t watermarkBytes = 0;
        InterruptPin interruptPin = InterruptPin::INT1;
    };

    struct InterruptConfig {
        bool outputEnabled = false;
        bool activeHigh = false;
        bool pushPull = false;
        bool latched = false;
    };

    /***************************************************************************
     * Listener Interface
     **************************************************************************/

    /**
     * @brief Interface for BMI270 event callbacks.
     *
     * Inherit from this class and override the methods you need.
     * All methods have empty default implementations.
     */
    class IListener
    {
    public:
        virtual ~IListener() = default;

        virtual void onStepCounter(const StepData& data) { (void)data; }
        virtual void onStepDetector(uint64_t timestamp) { (void)timestamp; }
        virtual void onActivity(const ActivityData& data) { (void)data; }
        virtual void onMotion(uint64_t timestamp) { (void)timestamp; }
        virtual void onNoMotion(uint64_t timestamp) { (void)timestamp; }
        virtual void onSigMotion(uint64_t timestamp) { (void)timestamp; }
        virtual void onWristWakeup(uint64_t timestamp) { (void)timestamp; }
        virtual void onWristGesture(const WristGestureData& data) { (void)data; }

        /**
         * @brief FIFO accelerometer data callback.
         * @param data: Array of raw accelerometer samples.
         * @param count: Number of samples in array.
         * @param scale: Scaling factor for conversion to m/s^2.
         * @param readTimeUs: System time, us, taken when the FIFO read that
         *        brought these samples was over. One value for every batch
         *        of that read, whichever callback it comes by.
         *
         * To convert raw data to physical units:
         *   float accel_ms2 = data[i].x * scale;  // m/s^2 (SI units)
         *
         * For other units, use driver conversion methods:
         *   float accel_g = rawToG(data[i].x);    // g (9.81 m/s^2)
         */
        virtual void onFifoAccel(const SensorRawData* data, uint16_t count, float scale, uint64_t readTimeUs)
        {
            (void)data; (void)count; (void)scale; (void)readTimeUs;
        }

        /**
         * @brief FIFO gyroscope data callback.
         * @param data: Array of raw gyroscope samples.
         * @param count: Number of samples in array.
         * @param scale: Scaling factor for conversion to rad/s.
         * @param readTimeUs: As for onFifoAccel().
         *
         * To convert raw data to physical units:
         *   float gyro_rads = data[i].x * scale;  // rad/s (SI units)
         *
         * For other units, use driver conversion methods:
         *   float gyro_dps = rawToDps(data[i].x); // deg/s (degrees per second)
         */
        virtual void onFifoGyro(const SensorRawData* data, uint16_t count, float scale, uint64_t readTimeUs)
        {
            (void)data; (void)count; (void)scale; (void)readTimeUs;
        }

        /**
         * @brief Synchronized FIFO data callback (accel + gyro pairs).
         * @param data: Array of synchronized samples (accel and gyro paired).
         * @param count: Number of sample pairs in array.
         * @param accelScale: Accelerometer scaling factor for conversion to m/s^2.
         * @param gyroScale: Gyroscope scaling factor for conversion to rad/s.
         * @param readTimeUs: As for onFifoAccel().
         *
         * This callback is triggered only when both accelerometer and gyroscope
         * are enabled with the same ODR (Sync Mode).
         *
         * To convert raw data to physical units:
         *   float accel_ms2 = data[i].accel_x * accelScale;  // m/s^2 (SI units)
         *   float gyro_rads = data[i].gyro_x * gyroScale;    // rad/s (SI units)
         *
         * A pair carries one counter value, the accelerometer frame's.
         */
        virtual void onFifoSync(const SyncSampleData* data, uint16_t count,
                                float accelScale, float gyroScale, uint64_t readTimeUs) {
            (void)data; (void)count; (void)accelScale; (void)gyroScale; (void)readTimeUs;
        }
    };

    /***************************************************************************
     * Construction / Configuration
     **************************************************************************/

    BMI270() = default;

    BMI270(Interface::II2c& i2c, uint8_t devAddr = kI2cAddr)
    {
        configure(i2c, devAddr);
    }

    ~BMI270() = default;

    /**
     * @brief Configure driver with I2C interface.
     * @param i2c: Reference to I2C interface.
     * @param devAddr: I2C device address (7-bit).
     * @retval true on success.
     */
    bool configure(Interface::II2c& i2c, uint8_t devAddr = kI2cAddr);

    /**
     * @brief Check if device responds on I2C bus.
     * @retval true if device is ready.
     */
    bool isReady() const;

    /**
     * @brief Get chip ID.
     * @retval Chip ID value (should be 0x24 for BMI270).
     */
    uint8_t getChipId();

    /***************************************************************************
     * Sensor Request/Release API (Primary Interface)
     **************************************************************************/

    /**
     * @brief Request a sensor to be enabled with configuration.
     * @param sensor: Sensor type to enable.
     * @param config: Sensor configuration.
     * @retval true on success.
     *
     * Handles lazy chip init and dependency management automatically.
     */
    bool requestSensor(SensorType sensor, const SensorConfig& config);

    /**
     * @brief Release a previously requested sensor.
     * @param sensor: Sensor type to disable.
     * @retval true on success.
     *
     * Handles reference counting and lazy deinit automatically.
     */
    bool releaseSensor(SensorType sensor);

    /**
     * @brief Update configuration of enabled sensor.
     * @param sensor: Sensor type to reconfigure.
     * @param config: New configuration.
     * @retval true on success.
     */
    bool updateSensorConfig(SensorType sensor, const SensorConfig& config);

    /**
     * @brief Request multiple sensors at once (batch operation).
     * @param sensors: Array of sensor types.
     * @param configs: Array of configurations.
     * @param count: Number of sensors.
     * @retval true on success.
     */
    bool requestSensorBatch(const SensorType* sensors,
                            const SensorConfig* configs,
                            uint8_t count);

    /**
     * @brief Release multiple sensors at once.
     * @param sensors: Array of sensor types.
     * @param count: Number of sensors.
     * @retval true on success.
     */
    bool releaseSensorBatch(const SensorType* sensors, uint8_t count);

    /**
     * @brief Release all sensors and deinitialize chip.
     * @retval true on success.
     */
    bool releaseAll();

    /***************************************************************************
     * Synchronized Sensor Mode (Sync Mode)
     **************************************************************************/

    /**
     * @brief Check if synchronized sensor mode is active.
     * @retval true if both accel and gyro enabled with same ODR and FIFO active.
     *
     * Sync mode is automatically detected when:
     * - Both accelerometer and gyroscope are enabled
     * - Both sensors have identical sample rates
     * - FIFO is enabled
     */
    bool isSyncMode() const;

    /***************************************************************************
     * FIFO Operations
     **************************************************************************/

    /**
     * @brief Configure and enable FIFO.
     * @param config: FIFO configuration.
     * @retval true on success.
     */
    bool configureFifo(const FifoConfig& config);

    /**
     * @brief Disable FIFO.
     * @retval true on success.
     */
    bool disableFifo();

    /**
     * @brief Process FIFO data and trigger callbacks.
     * @retval true on success.
     *
     * Call this from interrupt handler or polling loop.
     */
    bool processFifo();

    /**
     * @brief Flush FIFO buffer.
     * @retval true on success.
     */
    bool flushFifo();

    /**
     * @brief Get current FIFO fill level.
     * @retval Number of bytes in FIFO.
     */
    uint16_t getFifoLength();

    /**
     * @brief Calculate FIFO watermark for desired update period.
     * @param periodMs: Desired period between FIFO reads in milliseconds.
     * @param headerEnabled: True if header mode is used.
     * @param reserveMs: How late the read the watermark asks for may come;
     *                   the level is held low enough for the FIFO to take
     *                   what the sensors produce in that time.
     * @retval Watermark value in bytes, or 0 if no sensors configured.
     *
     * Calculates based on currently enabled sensors and their ODR.
     * Accounts for combined frames when ACC and GYR have same ODR.
     */
    uint16_t calculateFifoWatermark(uint32_t periodMs, bool headerEnabled, uint32_t reserveMs) const;

    /**
     * @brief Set FIFO watermark level.
     * @param bytes: Watermark in bytes.
     * @retval true on success.
     */
    bool setFifoWatermark(uint16_t bytes);

    /***************************************************************************
     * Direct Data Reading
     **************************************************************************/

    /**
     * @brief Read accelerometer data (scaled to m/s2).
     * @param data: Output data structure.
     * @retval true on success.
     */
    bool readAccelerometer(SensorData& data);

    /**
     * @brief Read accelerometer raw data.
     * @param data: Output data structure.
     * @retval true on success.
     */
    bool readAccelerometerRaw(SensorRawData& data);

    /**
     * @brief Read gyroscope data (scaled to rad/s).
     * @param data: Output data structure.
     * @retval true on success.
     */
    bool readGyroscope(SensorData& data);

    /**
     * @brief Read temperature.
     * @param temperature: Output temperature in Celsius.
     * @retval true on success.
     */
    bool readTemperature(float& temperature);

    /**
     * @brief Read step counter value.
     * @param data: Output step data.
     * @retval true on success.
     */
    bool readStepCounter(StepData& data);

    /**
     * @brief Read activity recognition result.
     * @param data: Output activity data.
     * @retval true on success.
     */
    bool readActivity(ActivityData& data);

    /**
     * @brief Reset step counter to zero.
     * @retval true on success.
     */
    bool resetStepCounter();

    /**
     * @brief Read wrist gesture detection result.
     * @param data: Output gesture data.
     * @retval true if a new gesture was detected since last enable or last read.
     *
     * Uses a software baseline to avoid returning stale output register values.
     * The BMI270 WR_GEST_OUT register holds the last detected gesture even when
     * the feature is disabled. This function returns true only when the register
     * value has changed from the baseline captured at sensor enable time.
     * Baseline is reset to UNKNOWN after each detection so repeated identical
     * gestures are reported correctly.
     */
    bool readWristGesture(WristGestureData& data);

    /**
     * @brief Configure WRIST_GESTURE detection parameters.
     * @param config: Wrist gesture configuration structure.
     * @retval true on success.
     *
     * Stores configuration that will be applied automatically when the sensor
     * is requested via requestSensor() or updateSensorConfig().
     * Can be called before or after sensor initialization.
     */
    bool configureWristGesture(const WristGestureConfig& config);

    /**
     * @brief Configure WRIST_WEAR_WAKE_UP detection parameters.
     * @param config: Wrist wakeup configuration structure.
     * @retval true on success.
     *
     * Stores configuration that will be applied automatically when the sensor
     * is requested via requestSensor() or updateSensorConfig().
     * Can be called before or after sensor initialization.
     */
    bool configureWristWakeup(const WristMotionConfig& config);

    /**
     * @brief Configure ANY_MOTION detection parameters.
     * @param config: Motion configuration structure.
     * @retval true on success.
     *
     * Stores configuration for ANY_MOTION detection that will be applied
     * automatically when sensor is requested via requestSensor() or updateSensorConfig().
     * Can be called before or after sensor initialization.
     * Threshold range: 0-1000mg (will be clamped to valid range).
     * Duration range: 0-163 samples (will be clamped to valid range).
     */
    bool configureAnyMotion(const MotionConfig& config);

    /**
     * @brief Configure NO_MOTION detection parameters.
     * @param config: Motion configuration structure.
     * @retval true on success.
     *
     * Stores configuration for NO_MOTION detection that will be applied
     * automatically when sensor is requested via requestSensor() or updateSensorConfig().
     * Can be called before or after sensor initialization.
     * Threshold range: 0-1000mg (will be clamped to valid range).
     * Duration range: 0-163 samples (will be clamped to valid range).
     */
    bool configureNoMotion(const MotionConfig& config);

    /***************************************************************************
     * Interrupt Configuration
     **************************************************************************/

    /**
     * @brief Configure interrupt pin.
     * @param pin: Interrupt pin (INT1 or INT2).
     * @param config: Pin configuration.
     * @retval true on success.
     *
     * Can be called before configureFifo() to store config for auto-apply,
     * or after to apply immediately.
     */
    bool configureInterruptPin(InterruptPin pin, const InterruptConfig& config);

    /**
     * @brief Set interrupt pin configuration for later auto-apply.
     * @param pin: Interrupt pin (INT1 or INT2).
     * @param config: Pin configuration to store.
     *
     * Stores config that will be auto-applied on first configureFifo() call.
     * Use this before chip initialization if you want FIFO to auto-configure interrupts.
     */
    void setInterruptConfig(InterruptPin pin, const InterruptConfig& config);

    /**
     * @brief Map sensor interrupt to pin.
     * @param sensor: Sensor type.
     * @param pin: Target interrupt pin.
     * @retval true on success.
     */
    bool mapSensorInterrupt(SensorType sensor, InterruptPin pin);

    /**
     * @brief Map data ready interrupt to pin.
     * @param accel: Map accelerometer data ready.
     * @param gyro: Map gyroscope data ready.
     * @param pin: Target interrupt pin.
     * @retval true on success.
     */
    bool mapDataReadyInterrupt(bool accel, bool gyro, InterruptPin pin);

    /**
     * @brief Read and clear interrupt status.
     * @param status: Output status value.
     * @retval true on success.
     */
    bool readInterruptStatus(uint16_t& status);

    /**
     * @brief Process interrupt (call from ISR or polling).
     *
     * Reads status and triggers appropriate callbacks.
     * Uses system time from callback set via setSystemTimeCallback().
     */
    void processInterrupt();

    /***************************************************************************
     * Listener Registration
     **************************************************************************/

    /**
     * @brief Set the event listener.
     * @param listener: Pointer to listener implementation (can be nullptr to disable).
     */
    void setListener(IListener* listener) { mpListener = listener; }

    /***************************************************************************
     * Power Management
     **************************************************************************/

    /**
     * @brief Enable/disable advanced power save mode.
     * @param enable: true to enable power save.
     * @retval true on success.
     */
    bool setAdvancedPowerSave(bool enable);

    /**
     * @brief Perform soft reset.
     * @retval true on success.
     */
    bool softReset();

    /**
     * @brief Perform accelerometer self-test.
     * @retval true if test passed.
     *
     * Initializes the chip if no requestSensor() has done so yet. The test
     * reconfigures and then soft-resets the part, so every sensor counts as
     * released afterwards and has to be requested again.
     */
    bool selfTest();

    /***************************************************************************
     * Status Queries
     **************************************************************************/

    /**
     * @brief Check if sensor is currently enabled.
     * @param sensor: Sensor type.
     * @retval true if enabled.
     */
    bool isSensorEnabled(SensorType sensor) const;

    /**
     * @brief Get current sensor configuration.
     * @param sensor: Sensor type.
     * @retval Current configuration.
     */
    SensorConfig getSensorConfig(SensorType sensor) const;

    /**
     * @brief Data period the accelerometer or the gyroscope runs at.
     * @param sensor: SensorType::ACCELEROMETER or SensorType::GYROSCOPE.
     * @retval Period in us, 0 when no rate has been set or for another sensor.
     *
     * Not the period asked for: the part has a ladder of rates and takes the
     * nearest one that is no slower, and the features hold the accelerometer
     * to kFeaturePeriodUs.
     */
    uint32_t getAppliedPeriodUs(SensorType sensor) const;

    /**
     * @brief Check if chip is initialized.
     * @retval true if initialized.
     */
    bool isChipInitialized() const { return mChipInitialized; }

    /**
     * @brief Get bitmask of enabled sensors.
     * @retval Bitmask (bit N = SensorType N).
     */
    uint16_t getEnabledSensorsMask() const;

    /**
     * @brief Get accelerometer reference count (for debugging).
     * @retval Number of features using accelerometer.
     */
    uint8_t getAccelRefCount() const { return mAccelRefCount; }

    /***************************************************************************
     * Unit Conversion Utilities
     **************************************************************************/

    /**
     * @brief Get accelerometer scaling factor.
     * @param  range Target accelerometer range.
     * @retval Accelerometer scaling factor for conversion raw value to m/s^2.
     */
    float getAccelScale(AccelRange range) const;

    /**
     * @brief Get gyroscope scaling factor.
     * @param  range Target gyroscope range.
     * @retval Gyroscope scaling factor for conversion raw value to rad/s.
     */
    float getGyroScale(GyroRange range) const;

    /**
     * @brief Convert raw accelerometer value to g.
     * @param raw: Raw ADC value.
     * @retval Value in g.
     */
    float rawToG(int16_t raw) const;

    /**
     * @brief Convert raw accelerometer value to m/s2.
     * @param raw: Raw ADC value.
     * @retval Value in m/s2.
     */
    float rawToMs2(int16_t raw) const;

    /**
     * @brief Convert raw gyroscope value to deg/s.
     * @param raw: Raw ADC value.
     * @retval Value in deg/s.
     */
    float rawToDps(int16_t raw) const;

    /**
     * @brief Convert raw gyroscope value to rad/s.
     * @param raw: Raw ADC value.
     * @retval Value in rad/s.
     */
    float rawToRads(int16_t raw) const;

    /**
     * @brief Get current sensor time.
     * @retval 24-bit sensor time value from BMI270 chip.
     */
    uint32_t getSensorTime();

    /**
     * @brief Set callback for system time retrieval (optional).
     * @param callback: Function pointer that returns time in microseconds.
     *
     * The callback should return system time in microseconds (uint64_t).
     * Example: Driver::System::GetTimeUs()
     *
     * If not set, driver falls back to Driver::System::GetTimeMs() * 1000.
     */
    void setSystemTimeCallback(uint64_t (*callback)()) { mGetSystemTime = callback; }

    /**
     * @brief The system time, us, on the clock the driver dates its events
     *        and FIFO reads by.
     */
    uint64_t getSystemTime();

private:

    /***************************************************************************
     * Internal State
     **************************************************************************/

    bool mIsConfigured = false;
    bool mChipInitialized = false;
    bool mFeatureEngineEnabled = false;
    bool mFifoEnabled = false;

    // Interrupt configuration storage
    InterruptConfig mInt1Config{};
    InterruptConfig mInt2Config{};
    bool mInt1Configured = false;
    bool mInt2Configured = false;

    uint8_t mAddr = kI2cAddr;
    Interface::II2c* mpI2c = nullptr;
    bmi2_dev mBmi2Dev{};

    // Sensor state
    SensorConfig mSensorConfigs[static_cast<uint8_t>(SensorType::MAX_SENSORS)]{};
    bool mSensorEnabled[static_cast<uint8_t>(SensorType::MAX_SENSORS)]{};
    uint8_t mAccelRefCount = 0;

    /// Rates the part was last set to, as register values; 0 for none yet.
    uint8_t mAccelOdr = 0;
    uint8_t mGyroOdr  = 0;
    uint32_t mPrevStepCount = 0;

    // Feature-specific configurations
    MotionConfig mAnyMotionConfig{};
    MotionConfig mNoMotionConfig{};
    WristMotionConfig mWristMotionConfig{};
    WristGestureConfig mWristGestureConfig{};

    // Wrist gesture output baseline (captures register value at enable time
    // to distinguish new gestures from stale output after re-enable).
    // Reset to UNKNOWN after each detection so repeated identical gestures fire.
    uint8_t mWristGestureBaseline = 0;

    // Scaling factors
    float mAccelScale = 2.0f * 9.80665f / 32768.0f;
    float mGyroScale = 250.0f * 3.14159265f / (180.0f * 32768.0f);

    // FIFO configuration
    FifoConfig mFifoConfig {};

    // FIFO buffers
    bmi2_fifo_frame mFifoFrame{};
    uint8_t mFifoBuffer[kFifoBufferSize]{};
    bmi2_sens_axes_data mFifoSensorData[kFifoMaxSamples]{};
    SensorRawData mFifoRawDataBuffer[kFifoMaxSamples]{};  // Shared output buffer for FIFO callbacks
    SyncSampleData mFifoSyncBuffer[kFifoMaxSamples]{};    // Buffer for synchronized samples

    // Listener
    IListener* mpListener = nullptr;

    // System time callback (returns time in microseconds)
    uint64_t (*mGetSystemTime)() = nullptr;

    /// Where each stream's FIFO frames stand on the part's counter, and the
    /// rate those frames were taken at. That rate follows the one written
    /// to the part only when the part says the change has taken effect.
    Bmi270Fifo::StreamGrid mAccelGrid;
    Bmi270Fifo::StreamGrid mGyroGrid;

    /***************************************************************************
     * Internal Methods - Chip Lifecycle
     **************************************************************************/

    bool initChip();
    bool deinitChip();
    void forgetChipState();
    bool shouldDeinitChip() const;

    /***************************************************************************
     * Internal Methods - Hardware Control
     **************************************************************************/

    BMI2_INTF_RETURN_TYPE applyAccelConfig(const SensorConfig& config);
    BMI2_INTF_RETURN_TYPE applyGyroConfig(const SensorConfig& config);
    BMI2_INTF_RETURN_TYPE applyStepCounterConfig(const SensorConfig& config);
    BMI2_INTF_RETURN_TYPE applyAnyMotionConfig(const MotionConfig& config);
    BMI2_INTF_RETURN_TYPE applyNoMotionConfig(const MotionConfig& config);
    BMI2_INTF_RETURN_TYPE applyWristMotionConfig(const WristMotionConfig& config);
    BMI2_INTF_RETURN_TYPE applyWristGestureConfig(const WristGestureConfig& config);

    BMI2_INTF_RETURN_TYPE enableAccelHardware();
    BMI2_INTF_RETURN_TYPE disableAccelHardware();
    BMI2_INTF_RETURN_TYPE enableGyroHardware();
    BMI2_INTF_RETURN_TYPE disableGyroHardware();
    BMI2_INTF_RETURN_TYPE enableTemperatureHardware();
    BMI2_INTF_RETURN_TYPE disableTemperatureHardware();
    BMI2_INTF_RETURN_TYPE enableFeatureHardware(SensorType sensor);
    BMI2_INTF_RETURN_TYPE disableFeatureHardware(SensorType sensor);

    bool enableAccelInternal();
    void updateScalingFactors();

    /// Read what the FIFO holds into mFifoBuffer, whole frames only.
    bool readFifo(uint16_t fifoLength);

    /// Whether the FIFO gives what it takes to place its frames on the
    /// part's counter: headers, and the sensortime frame.
    bool fifoIsTimed() const;

    /// Give the listener the frames of a stretch of mFifoBuffer.
    void deliverFrames(uint16_t begin, uint16_t end, uint64_t timestamp, const uint32_t* endTime, bool skipped);

    /// Whether the part has a sensor switched on, by its bit in PWR_CTRL.
    bool isPowered(uint8_t mask);

    /// Take in an InputConfig frame of the FIFO.
    void applyInputConfig(const Bmi270Fifo::Frame& frame);

    /// A stream's rate was written to the part: tell its frames' grid.
    void rateWritten(Bmi270Fifo::StreamGrid& grid, uint8_t odr, bool running, bool inFifo);

    /***************************************************************************
     * Internal Methods - Conversion Utilities
     **************************************************************************/

    /// Rate for an accelerometer period asked for, with the features' say.
    uint8_t accelOdrFor(uint32_t periodUs) const;

    /// Set the accelerometer again after the features' need of it changed.
    /// Returns whether its rate moved.
    bool retuneAccel();

    uint32_t subtractWithOverflow(uint32_t timestamp, uint32_t ticks) const;
    uint64_t ticksToUs(uint32_t ticks) const;

    /***************************************************************************
     * Internal Methods - I2C Communication
     **************************************************************************/

    bool readRegister(uint8_t reg, uint8_t* data, uint32_t len);
    bool writeRegister(uint8_t reg, const uint8_t* data, uint32_t len);

    static BMI2_INTF_RETURN_TYPE bmi2Read(uint8_t regAddr, uint8_t* regData,
                                          uint32_t len, void* intfPtr);
    static BMI2_INTF_RETURN_TYPE bmi2Write(uint8_t regAddr, const uint8_t* regData,
                                           uint32_t len, void* intfPtr);
    static void bmi2Delay(uint32_t periodUs, void* intfPtr);
};

} // namespace Hardware

#endif // HARDWARE_BMI270_HPP
