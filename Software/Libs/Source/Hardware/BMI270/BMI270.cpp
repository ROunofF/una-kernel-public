/**
 ******************************************************************************
 * @file    BMI270.cpp
 * @brief   BMI270 IMU driver.
 ******************************************************************************
 *
 * Sensor management:
 * - Reference counting for accelerometer dependency
 * - Lazy chip init/deinit when no sensors are active
 * - Separated configure vs enable phases
 * - Batch sensor enable/disable operations
 *
 ******************************************************************************
 */

#include "Hardware/BMI270/BMI270.hpp"

#define LOG_MODULE_PRX      "BMI270"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "bmi270_context.h"
#include "bmi270.h"
#include "bmi2.h"
#include "System.hpp"
#include "UsTimer.hpp"
#include "OS.hpp"

#include <algorithm>
#include <inttypes.h>

namespace Hardware
{

/**
 * @brief Convert SensorType to BMI2 sensor/feature type.
 * @param sensor: SensorType enum value.
 * @return BMI2 sensor type constant or 0xFF if not supported.
 */
static uint8_t sensorTypeToBmi2(BMI270::SensorType sensor)
{
    switch (sensor) {
        case BMI270::SensorType::ACCELEROMETER:
            return BMI2_ACCEL;
        case BMI270::SensorType::GYROSCOPE:
            return BMI2_GYRO;
        case BMI270::SensorType::STEP_COUNTER:
            return BMI2_STEP_COUNTER;
        case BMI270::SensorType::STEP_DETECTOR:
            return BMI2_STEP_DETECTOR;
        case BMI270::SensorType::ACTIVITY_RECOGNITION:
            return BMI2_STEP_ACTIVITY;
        case BMI270::SensorType::WRIST_WAKEUP:
            return BMI2_WRIST_WEAR_WAKE_UP;
        case BMI270::SensorType::WRIST_GESTURE:
            return BMI2_WRIST_GESTURE;
        case BMI270::SensorType::NO_MOTION:
            return BMI2_NO_MOTION;
        case BMI270::SensorType::ANY_MOTION:
            return BMI2_ANY_MOTION;
        case BMI270::SensorType::SIG_MOTION:
            return BMI2_SIG_MOTION;
        default:
            return 0xFF;
    }
}

/**
 * @brief Check if sensor type requires accelerometer to function.
 * @param sensor: SensorType to check.
 * @return True if sensor depends on accelerometer data.
 */
static bool sensorRequiresAccel(BMI270::SensorType sensor)
{
    switch (sensor) {
        case BMI270::SensorType::STEP_COUNTER:
        case BMI270::SensorType::STEP_DETECTOR:
        case BMI270::SensorType::ACTIVITY_RECOGNITION:
        case BMI270::SensorType::WRIST_WAKEUP:
        case BMI270::SensorType::WRIST_GESTURE:
        case BMI270::SensorType::NO_MOTION:
        case BMI270::SensorType::ANY_MOTION:
        case BMI270::SensorType::SIG_MOTION:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if sensor type is a BMI270 feature (not basic sensor).
 * @param sensor: SensorType to check.
 * @return True if sensor is a feature engine sensor.
 */
static bool isFeatureSensor(BMI270::SensorType sensor)
{
    switch (sensor) {
        case BMI270::SensorType::STEP_COUNTER:
        case BMI270::SensorType::STEP_DETECTOR:
        case BMI270::SensorType::ACTIVITY_RECOGNITION:
//        case BMI270::SensorType::WRIST_WAKEUP:
        case BMI270::SensorType::NO_MOTION:
        case BMI270::SensorType::ANY_MOTION:
        case BMI270::SensorType::SIG_MOTION:
            return true;
        default:
            return false;
    }
}

/*******************************************************************************
 * Construction / Configuration
 ******************************************************************************/

bool BMI270::configure(Interface::II2c& i2c, uint8_t devAddr)
{
    mpI2c = &i2c;
    mAddr = devAddr;
    mIsConfigured = true;

    // Initialize sensor configurations to default values
    for (int i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        mSensorConfigs[i].periodUs = kFeaturePeriodUs;
        mSensorConfigs[i].bandwidth = Bandwidth::NORM_AVG4;
        mSensorConfigs[i].performanceMode = false;
        mSensorConfigs[i].watermarkLevel = 0;

        if (i == static_cast<uint8_t>(SensorType::ACCELEROMETER)) {
            mSensorConfigs[i].range = 2;
        } else if (i == static_cast<uint8_t>(SensorType::GYROSCOPE)) {
            mSensorConfigs[i].range = 250;
        } else {
            mSensorConfigs[i].range = 0;
        }
    }

    // Setup BMI2 device structure
    Driver::UsTimer::Init();

    mBmi2Dev.read = bmi2Read;
    mBmi2Dev.write = bmi2Write;
    mBmi2Dev.delay_us = bmi2Delay;
    mBmi2Dev.intf = BMI2_I2C_INTF;
    mBmi2Dev.intf_ptr = this;
    mBmi2Dev.config_file_ptr = nullptr;
    mBmi2Dev.config_size = 0;
    mBmi2Dev.read_write_len = 32;  // I2C burst length limit

    return true;
}

bool BMI270::isReady() const
{
    return mIsConfigured && mpI2c->isDeviceReady(mAddr);
}

uint8_t BMI270::getChipId()
{
    uint8_t chipId = 0;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_regs(BMI2_CHIP_ID_ADDR, &chipId, 1, &mBmi2Dev);

    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to read chip ID\n");
        return 0;
    }

    return chipId;
}

/*******************************************************************************
 * I2C Communication
 ******************************************************************************/

BMI2_INTF_RETURN_TYPE BMI270::bmi2Read(uint8_t regAddr, uint8_t* regData,
                                       uint32_t len, void* intfPtr)
{
    if (intfPtr == nullptr || regData == nullptr) {
        return BMI2_E_NULL_PTR;
    }

    BMI270* instance = reinterpret_cast<BMI270*>(intfPtr);
    return instance->readRegister(regAddr, regData, len) ? BMI2_OK : BMI2_E_COM_FAIL;
}

BMI2_INTF_RETURN_TYPE BMI270::bmi2Write(uint8_t regAddr, const uint8_t* regData,
                                        uint32_t len, void* intfPtr)
{
    if (intfPtr == nullptr || regData == nullptr) {
        return BMI2_E_NULL_PTR;
    }

    BMI270* instance = reinterpret_cast<BMI270*>(intfPtr);
    return instance->writeRegister(regAddr, regData, len) ? BMI2_OK : BMI2_E_COM_FAIL;
}

void BMI270::bmi2Delay(uint32_t periodUs, void* intfPtr)
{
    if (periodUs < 1000) {
        Driver::UsTimer::DelayUs(periodUs);
    } else {
        uint32_t delayMs = (periodUs + 999) / 1000;
        OS::Delay(delayMs);
    }
}

bool BMI270::readRegister(uint8_t reg, uint8_t* data, uint32_t len)
{
    if (!mpI2c || !data) {
        return false;
    }

    bool success = false;
    for (uint32_t retry = 0; retry <= kI2cRetries; retry++) {
        if (mpI2c->readMem(mAddr, static_cast<uint16_t>(reg), 1, data, static_cast<uint16_t>(len))) {
            success = true;
            break;
        }

        if (retry < kI2cRetries) {
            LOG_WARNING("I2C read retry %lu\n", retry + 1);
            bmi2Delay(1000, this);
        }
    }

    if (!success) {
        LOG_ERROR("Failed to read register 0x%02X after %lu retries\n", reg, kI2cRetries);
    }

    return success;
}

bool BMI270::writeRegister(uint8_t reg, const uint8_t* data, uint32_t len)
{
    if (!mpI2c || !data) {
        return false;
    }

    bool success = false;
    for (uint32_t retry = 0; retry <= kI2cRetries; retry++) {
        if (mpI2c->writeMem(mAddr, static_cast<uint16_t>(reg), 1, data, static_cast<uint16_t>(len))) {
            success = true;
            break;
        }

        if (retry < kI2cRetries) {
            LOG_WARNING("I2C write retry %lu\n", retry + 1);
            bmi2Delay(1000, this);
        }
    }

    if (!success) {
        LOG_ERROR("Failed to write register 0x%02X after %lu retries\n", reg, kI2cRetries);
    }

    return success;
}

/*******************************************************************************
 * Sensor Request/Release API
 ******************************************************************************/

/**
 * @brief Request a sensor to be enabled with specific configuration.
 *
 * This function handles:
 * - Lazy chip initialization on first sensor request
 * - Reference counting for accelerometer dependency
 * - Proper enable sequence using Bosch API
 *
 * @param sensor: Sensor type to request.
 * @param config: Desired sensor configuration.
 * @return True on success, false on failure.
 */
bool BMI270::requestSensor(SensorType sensor, const SensorConfig& config)
{
    if (!mIsConfigured) {
        LOG_ERROR("Device not configured\n");
        return false;
    }

    uint8_t sensorIndex = static_cast<uint8_t>(sensor);
    if (sensorIndex >= static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        LOG_ERROR("Invalid sensor type\n");
        return false;
    }

    // Check if already enabled
    if (mSensorEnabled[sensorIndex]) {
        LOG_DEBUG("Sensor %d already enabled, updating config\n", sensorIndex);
        return updateSensorConfig(sensor, config);
    }

    // Lazy chip initialization
    if (!mChipInitialized) {
        if (!initChip()) {
            LOG_ERROR("Failed to initialize chip\n");
            return false;
        }
    }

    // Store configuration. What was there comes back if the request fails:
    // a setting that was refused is not left for a later request to use.
    const SensorConfig previous = mSensorConfigs[sensorIndex];

    mSensorConfigs[sensorIndex] = config;
    mSensorEnabled[sensorIndex] = true;

    // Handle accelerometer dependency
    if (sensorRequiresAccel(sensor)) {
        mAccelRefCount++;
        LOG_DEBUG("Accel ref count increased to %d\n", mAccelRefCount);

        // Enable accelerometer if this is first user
        if (mAccelRefCount == 1 &&
            !mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)]) {
            if (!enableAccelInternal()) {
                mAccelRefCount--;
                mSensorEnabled[sensorIndex] = false;
                mSensorConfigs[sensorIndex] = previous;
                return false;
            }
        }
    }

    // Apply configuration and enable sensor
    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    switch (sensor) {
        case SensorType::ACCELEROMETER:
            rslt = applyAccelConfig(config);
            if (rslt == BMI2_OK) {
                rslt = enableAccelHardware();
            }
            break;

        case SensorType::GYROSCOPE:
            rslt = applyGyroConfig(config);
            if (rslt == BMI2_OK) {
                rslt = enableGyroHardware();
            }
            break;

        case SensorType::TEMPERATURE:
            // Temperature needs to be enabled manually if gyroscope is not running
            // If gyroscope is already enabled, temperature is automatically available
            if (!mSensorEnabled[static_cast<uint8_t>(SensorType::GYROSCOPE)]) {
                rslt = enableTemperatureHardware();
            }
            break;

        case SensorType::STEP_COUNTER:
            rslt = applyStepCounterConfig(config);
            if (rslt == BMI2_OK) {
                rslt = enableFeatureHardware(sensor);
            }
            break;

        case SensorType::ANY_MOTION:
            rslt = applyAnyMotionConfig(mAnyMotionConfig);
            if (rslt == BMI2_OK) {
                rslt = enableFeatureHardware(sensor);
            }
            break;

        case SensorType::NO_MOTION:
            rslt = applyNoMotionConfig(mNoMotionConfig);
            if (rslt == BMI2_OK) {
                rslt = enableFeatureHardware(sensor);
            }
            break;

        case SensorType::WRIST_WAKEUP:
            rslt = applyWristMotionConfig(mWristMotionConfig);
            if (rslt == BMI2_OK) {
                rslt = enableFeatureHardware(sensor);
            }
            break;

        case SensorType::WRIST_GESTURE:
            rslt = applyWristGestureConfig(mWristGestureConfig);
            if (rslt == BMI2_OK) {
                rslt = enableFeatureHardware(sensor);
            }
            if (rslt == BMI2_OK) {
                // Capture stale output register value so readWristGesture()
                // will not report it as a new gesture after enable.
                struct bmi2_feat_sensor_data featData{};
                featData.type = BMI2_WRIST_GESTURE;
                if (bmi270_get_feature_data(&featData, 1, &mBmi2Dev) == BMI2_OK) {
                    mWristGestureBaseline = featData.sens_data.wrist_gesture_output;
                }
            }
            break;

        default:
            if (isFeatureSensor(sensor)) {
                rslt = enableFeatureHardware(sensor);
            } else {
                LOG_ERROR("Unsupported sensor type %d\n", sensorIndex);
                mSensorEnabled[sensorIndex] = false;
                return false;
            }
            break;
    }

    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to enable sensor %d, error %d\n", sensorIndex, rslt);

        // Rollback
        mSensorEnabled[sensorIndex] = false;
        mSensorConfigs[sensorIndex] = previous;
        if (sensorRequiresAccel(sensor)) {
            mAccelRefCount--;
        }
        return false;
    }

    // A feature that has just come on may need the accelerometer faster than
    // was asked for it.
    if (sensorRequiresAccel(sensor)) {
        retuneAccel();
    }

    updateScalingFactors();

    LOG_DEBUG("Sensor %d enabled, accel period %lu us, gyro period %lu us\n", sensorIndex,
              static_cast<unsigned long>(Bmi270Odr::periodUs(mAccelOdr)),
              static_cast<unsigned long>(Bmi270Odr::periodUs(mGyroOdr)));
    return true;
}

/**
 * @brief Release a previously requested sensor.
 *
 * This function handles:
 * - Reference counting for accelerometer dependency
 * - Lazy chip deinitialization when all sensors are released
 *
 * @param sensor: Sensor type to release.
 * @return True on success, false on failure.
 */
bool BMI270::releaseSensor(SensorType sensor)
{
    if (!mIsConfigured) {
        LOG_ERROR("Device not configured\n");
        return false;
    }

    uint8_t sensorIndex = static_cast<uint8_t>(sensor);
    if (sensorIndex >= static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        LOG_ERROR("Invalid sensor type\n");
        return false;
    }

    // Check if actually enabled
    if (!mSensorEnabled[sensorIndex]) {
        LOG_DEBUG("Sensor %d not enabled\n", sensorIndex);
        return true;
    }

    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    // Disable sensor hardware
    switch (sensor) {
        case SensorType::ACCELEROMETER:
            // Only disable if no other users
            if (mAccelRefCount == 0) {
                rslt = disableAccelHardware();
            } else {
                LOG_DEBUG("Accel still has %d users, keeping enabled\n", mAccelRefCount);

                uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);
                SensorConfig& config = mSensorConfigs[accelIndex];

                // Process remaining FIFO data with old ODR before reconfiguring
                if (mFifoEnabled) {
                    processFifo();
                }

                // What the features need and nothing more
                config.periodUs = kFeaturePeriodUs;
                config.range = 2;
                config.bandwidth = Bandwidth::NORM_AVG4;
                config.performanceMode = false;

                rslt = applyAccelConfig(config);
                if (rslt == BMI2_OK) {

                    LOG_DEBUG("Accel reconfigured for the features, period %lu us\n",
                              static_cast<unsigned long>(Bmi270Odr::periodUs(mAccelOdr)));
                    updateScalingFactors();
                }
            }
            break;

        case SensorType::GYROSCOPE:
            rslt = disableGyroHardware();
            break;

        case SensorType::TEMPERATURE:
            // Only disable temperature if gyroscope is not running
            // (gyroscope automatically enables temperature)
            if (!mSensorEnabled[static_cast<uint8_t>(SensorType::GYROSCOPE)]) {
                rslt = disableTemperatureHardware();
            }
            break;

        default:
            if (isFeatureSensor(sensor)) {
                rslt = disableFeatureHardware(sensor);
            }
            break;
    }

    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to disable sensor %d, error %d\n", sensorIndex, rslt);
        return false;
    }

    // Update reference counting
    if (sensorRequiresAccel(sensor)) {
        if (mAccelRefCount > 0) {
            mAccelRefCount--;
            LOG_DEBUG("Accel ref count decreased to %d\n", mAccelRefCount);

            // Disable accelerometer if no more users
            if (mAccelRefCount == 0 &&
                !mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)]) {
                disableAccelHardware();
            }

            // With the last feature gone the accelerometer may go back to
            // what was asked for it.
            retuneAccel();
        }
    }

    mSensorEnabled[sensorIndex] = false;

    // Check if all sensors are disabled for lazy deinit
    if (shouldDeinitChip()) {
        LOG_DEBUG("All sensors disabled, deinitializing chip\n");
        deinitChip();
    }

    LOG_DEBUG("Sensor %d released\n", sensorIndex);
    return true;
}

/**
 * @brief Update configuration of an already enabled sensor.
 * @param sensor: Sensor type to reconfigure.
 * @param config: New configuration.
 * @return True on success, false on failure.
 */
bool BMI270::updateSensorConfig(SensorType sensor, const SensorConfig& config)
{
    if (!mIsConfigured) {
        LOG_ERROR("Device not configured\n");
        return false;
    }

    uint8_t sensorIndex = static_cast<uint8_t>(sensor);
    if (sensorIndex >= static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        LOG_ERROR("Invalid sensor type\n");
        return false;
    }

    if (!mSensorEnabled[sensorIndex]) {
        LOG_ERROR("Sensor %d not enabled, use requestSensor instead\n", sensorIndex);
        return false;
    }

    SensorConfig oldConfig = mSensorConfigs[sensorIndex];

    // Check if ODR is changing
    bool odrChanging = (oldConfig.periodUs != config.periodUs);

    // Process remaining FIFO data with old ODR before changing
    if (odrChanging && mFifoEnabled) {
        processFifo();
    }

    mSensorConfigs[sensorIndex] = config;
    mSensorEnabled[sensorIndex] = true;

    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    switch (sensor) {
        case SensorType::ACCELEROMETER:
            rslt = applyAccelConfig(config);
            break;

        case SensorType::GYROSCOPE:
            rslt = applyGyroConfig(config);
            break;

        case SensorType::STEP_COUNTER:
            rslt = applyStepCounterConfig(config);
            break;

        case SensorType::ANY_MOTION:
            rslt = applyAnyMotionConfig(mAnyMotionConfig);
            break;

        case SensorType::NO_MOTION:
            rslt = applyNoMotionConfig(mNoMotionConfig);
            break;

        case SensorType::WRIST_WAKEUP:
            rslt = applyWristMotionConfig(mWristMotionConfig);
            break;

        case SensorType::WRIST_GESTURE:
            rslt = applyWristGestureConfig(mWristGestureConfig);
            break;

        default:
            // Feature sensors typically don't have runtime config
            break;
    }

    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to update sensor %d, error %d\n", sensorIndex, rslt);
        mSensorConfigs[sensorIndex] = oldConfig;

        // Settings the part cannot run are refused before they are written.
        // Should it report some all the same, it has taken them by then and
        // goes on with them: put back the ones it ran with, or it and this
        // driver disagree on the rate.
        if (sensor == SensorType::ACCELEROMETER) {
            applyAccelConfig(oldConfig);
        } else if (sensor == SensorType::GYROSCOPE) {
            applyGyroConfig(oldConfig);
        }

        return false;
    }

    updateScalingFactors();
    LOG_DEBUG("Sensor %d config updated\n", sensorIndex);
    return true;
}

/*******************************************************************************
 * Synchronized Sensor Mode
 ******************************************************************************/

/**
 * @brief Check if synchronized sensor mode is active.
 * @return True if both accel and gyro enabled with same ODR and FIFO active.
 */
bool BMI270::isSyncMode() const
{
    uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);
    uint8_t gyroIndex  = static_cast<uint8_t>(SensorType::GYROSCOPE);

    return mSensorEnabled[accelIndex] &&
           mSensorEnabled[gyroIndex] &&
           mAccelOdr == mGyroOdr &&
           mFifoEnabled;
}

/*******************************************************************************
 * Internal Hardware Control Functions
 ******************************************************************************/

/**
 * @brief Initialize BMI270 chip (lazy initialization).
 * @return True on success.
 */
bool BMI270::initChip()
{
    if (mChipInitialized) {
        return true;
    }

    BMI2_INTF_RETURN_TYPE rslt = bmi270_init(&mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("bmi270_init failed: %d\n", rslt);
        return false;
    }

    struct bmi2_remap remap;
    remap.x = BMI2_NEG_Y;
    remap.y = BMI2_X;
    remap.z = BMI2_Z;

    rslt = bmi2_set_remap_axes(&remap, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("bmi2_set_remap_axes failed: %d\n", rslt);
        return false;
    }

    // Disable advance power save for better responsiveness
    rslt = bmi2_set_adv_power_save(BMI2_DISABLE, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("bmi2_set_adv_power_save failed: %d\n", rslt);
        return false;
    }

    mChipInitialized = true;
    LOG_DEBUG("Chip initialized\n");
    return true;
}

/**
 * @brief Deinitialize BMI270 chip (lazy deinitialization).
 * @return True on success.
 */
bool BMI270::deinitChip()
{
    if (!mChipInitialized) {
        return true;
    }

    // Enable power save mode
    bmi2_set_adv_power_save(BMI2_ENABLE, &mBmi2Dev);

    // Optionally perform soft reset
    bmi2_set_command_register(BMI2_SOFT_RESET_CMD, &mBmi2Dev);
    OS::Delay(10);

    mChipInitialized = false;
    mFeatureEngineEnabled = false;
    mFifoEnabled = false;
    mFifoConfig = {};
    mAccelRefCount = 0;
    mAccelOdr = 0;
    mGyroOdr  = 0;
    mAccelGrid.restart();
    mGyroGrid.restart();

    // Reset interrupt configured flags so they get reconfigured on next init
    mInt1Configured = false;
    mInt2Configured = false;

    // Clear sensor configurations to ensure clean state on next init
    for (uint8_t i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        mSensorConfigs[i] = SensorConfig{};
    }

    LOG_DEBUG("Chip deinitialized\n");
    return true;
}

/**
 * @brief Mark the chip uninitialized, every sensor released and the FIFO and
 *        interrupt pins unconfigured, as a reset leaves them.
 *
 * The stored pin configurations are kept, so configureFifo() applies them
 * again.
 */
void BMI270::forgetChipState()
{
    mChipInitialized = false;
    mFeatureEngineEnabled = false;
    mFifoEnabled = false;
    mInt1Configured = false;
    mInt2Configured = false;
    mAccelRefCount = 0;
    mAccelOdr = 0;
    mGyroOdr  = 0;
    mAccelGrid.restart();
    mGyroGrid.restart();
    for (int i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        mSensorEnabled[i] = false;
    }
}

/**
 * @brief Check if chip should be deinitialized (all sensors disabled).
 * @return True if no sensors are active.
 */
bool BMI270::shouldDeinitChip() const
{
    for (int i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        if (mSensorEnabled[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Enable accelerometer for internal dependency (feature sensors).
 * @return True on success.
 */
bool BMI270::enableAccelInternal()
{
    // Use default config if accelerometer is not explicitly configured
    uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);
    SensorConfig& config = mSensorConfigs[accelIndex];

    // Apply default config if not set
    if (config.periodUs == 0) {
        config.periodUs = kFeaturePeriodUs;
        config.range = 2;        // 2g range
        config.bandwidth = Bandwidth::NORM_AVG4;
        config.performanceMode = false;
        LOG_DEBUG("Using default accel config: period=%lu us, range=%u g\n",
                  static_cast<unsigned long>(config.periodUs), config.range);
    }

    BMI2_INTF_RETURN_TYPE rslt = applyAccelConfig(config);
    if (rslt != BMI2_OK) {
        LOG_ERROR("applyAccelConfig failed: %d\n", rslt);
        return false;
    }

    LOG_DEBUG("Accel internal enabled: period=%lu us\n",
              static_cast<unsigned long>(Bmi270Odr::periodUs(mAccelOdr)));

    rslt = enableAccelHardware();
    return (rslt == BMI2_OK);
}

/**
 * @brief Apply accelerometer configuration without enabling.
 * @param config: Configuration to apply.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::applyAccelConfig(const SensorConfig& config)
{
    struct bmi2_sens_config sens_cfg;
    sens_cfg.type = BMI2_ACCEL;

    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_sensor_config(&sens_cfg, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    // Set ODR
    const uint8_t odr = accelOdrFor(config.periodUs);
    sens_cfg.cfg.acc.odr = odr;

    // Set range
    switch (config.range) {
        case 2:  sens_cfg.cfg.acc.range = BMI2_ACC_RANGE_2G; break;
        case 4:  sens_cfg.cfg.acc.range = BMI2_ACC_RANGE_4G; break;
        case 8:  sens_cfg.cfg.acc.range = BMI2_ACC_RANGE_8G; break;
        case 16: sens_cfg.cfg.acc.range = BMI2_ACC_RANGE_16G; break;
        default: sens_cfg.cfg.acc.range = BMI2_ACC_RANGE_2G; break;
    }

    // Set bandwidth
    sens_cfg.cfg.acc.bwp = static_cast<uint8_t>(config.bandwidth);

    // Set performance mode
    sens_cfg.cfg.acc.filter_perf = config.performanceMode ?
                                   BMI2_PERF_OPT_MODE : BMI2_POWER_OPT_MODE;

    if (!Bmi270Odr::accelSettingsValid(odr, config.performanceMode, static_cast<uint8_t>(config.bandwidth))) {
        LOG_ERROR("Accelerometer settings the part cannot run: %lu us, performance %d, averaging %u\n",
                  static_cast<unsigned long>(Bmi270Odr::periodUs(odr)), config.performanceMode,
                  static_cast<unsigned>(config.bandwidth));
        return BMI2_E_ACC_INVALID_CFG;
    }

    const bool running = isPowered(BMI2_ACC_EN_MASK);

    rslt = bmi2_set_sensor_config(&sens_cfg, 1, &mBmi2Dev);
    if (rslt == BMI2_OK) {
        mAccelOdr = odr;
        rateWritten(mAccelGrid, odr, running, mFifoConfig.accelEnabled);
    }

    return rslt;
}

/**
 * @brief Enable accelerometer hardware.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::enableAccelHardware()
{
    const uint8_t sens_list[] = {BMI2_ACCEL};
    return bmi2_sensor_enable(sens_list, 1, &mBmi2Dev);
}

/**
 * @brief Disable accelerometer hardware.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::disableAccelHardware()
{
    const uint8_t sens_list[] = {BMI2_ACCEL};
    return bmi2_sensor_disable(sens_list, 1, &mBmi2Dev);
}

/**
 * @brief Apply gyroscope configuration without enabling.
 * @param config: Configuration to apply.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::applyGyroConfig(const SensorConfig& config)
{
    struct bmi2_sens_config sens_cfg;
    sens_cfg.type = BMI2_GYRO;

    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_sensor_config(&sens_cfg, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("bmi2_get_sensor_config failed: %d\n", rslt);
        return rslt;
    }

    // Set ODR
    const uint8_t odr = Bmi270Odr::fromPeriodUs(config.periodUs, Bmi270Odr::kGyroSlowest);
    sens_cfg.cfg.gyr.odr = odr;

    // Set range
    switch (config.range) {
        case 125:  sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_125; break;
        case 250:  sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_250; break;
        case 500:  sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_500; break;
        case 1000: sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_1000; break;
        case 2000: sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_2000; break;
        default:   sens_cfg.cfg.gyr.range = BMI2_GYR_RANGE_250; break;
    }

    // Set bandwidth
    sens_cfg.cfg.gyr.bwp = static_cast<uint8_t>(config.bandwidth);

    // Set performance mode
    sens_cfg.cfg.gyr.filter_perf = config.performanceMode ?
                                   BMI2_PERF_OPT_MODE : BMI2_POWER_OPT_MODE;

    // Set noise performance (required for valid config)
    sens_cfg.cfg.gyr.noise_perf = config.performanceMode ?
                                  BMI2_PERF_OPT_MODE : BMI2_POWER_OPT_MODE;

    if (!Bmi270Odr::gyroSettingsValid(odr, config.performanceMode)) {
        LOG_ERROR("Gyroscope settings the part cannot run: %lu us, performance %d\n",
                  static_cast<unsigned long>(Bmi270Odr::periodUs(odr)), config.performanceMode);
        return BMI2_E_GYRO_INVALID_CFG;
    }

    const bool running = isPowered(BMI2_GYR_EN_MASK);

    rslt = bmi2_set_sensor_config(&sens_cfg, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("bmi2_set_sensor_config failed: %d\n", rslt);
    } else {
        mGyroOdr = odr;
        rateWritten(mGyroGrid, odr, running, mFifoConfig.gyroEnabled);
    }
    return rslt;
}

/**
 * @brief Enable gyroscope hardware.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::enableGyroHardware()
{
    const uint8_t sens_list[] = {BMI2_GYRO};
    return bmi2_sensor_enable(sens_list, 1, &mBmi2Dev);
}

/**
 * @brief Disable gyroscope hardware.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::disableGyroHardware()
{
    const uint8_t sens_list[] = {BMI2_GYRO};
    return bmi2_sensor_disable(sens_list, 1, &mBmi2Dev);
}

/**
 * @brief Enable temperature sensor hardware.
 * @return BMI2_OK on success.
 * @note Temperature sensor must be manually enabled when gyroscope is off.
 *       When gyroscope is enabled, temperature is automatically available.
 */
BMI2_INTF_RETURN_TYPE BMI270::enableTemperatureHardware()
{
    // Read current PWR_CTRL register
    uint8_t pwrCtrl = 0;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_regs(BMI2_PWR_CTRL_ADDR, &pwrCtrl, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    // Set temp_en bit (bit 3)
    pwrCtrl |= BMI2_TEMP_EN_MASK;

    return bmi2_set_regs(BMI2_PWR_CTRL_ADDR, &pwrCtrl, 1, &mBmi2Dev);
}

/**
 * @brief Disable temperature sensor hardware.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::disableTemperatureHardware()
{
    // Read current PWR_CTRL register
    uint8_t pwrCtrl = 0;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_regs(BMI2_PWR_CTRL_ADDR, &pwrCtrl, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    // Clear temp_en bit (bit 3)
    pwrCtrl &= ~BMI2_TEMP_EN_MASK;

    return bmi2_set_regs(BMI2_PWR_CTRL_ADDR, &pwrCtrl, 1, &mBmi2Dev);
}

/**
 * @brief Apply step counter configuration.
 * @param config: Configuration to apply.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::applyStepCounterConfig(const SensorConfig& config)
{
    struct bmi2_sens_config conf;
    conf.type = BMI2_STEP_COUNTER;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    conf.cfg.step_counter.watermark_level = config.watermarkLevel;

    return bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);
}

BMI2_INTF_RETURN_TYPE BMI270::applyAnyMotionConfig(const MotionConfig& config)
{
    struct bmi2_sens_config conf {};
    conf.type = BMI2_ANY_MOTION;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    // Clamp threshold to 0-1000 mg, convert to BMI270 units
    // BMI270 threshold is 11-bit value: threshold = (threshold_mg * 2^11) / 1000
    uint16_t thresholdClamped = (config.threshold > 1000) ? 1000 : config.threshold;
    conf.cfg.any_motion.threshold = (thresholdClamped * 2048) / 1000;

    // Clamp duration to 0-163 samples (8-bit value 0-163)
    conf.cfg.any_motion.duration = (config.duration > 163) ? 163 : config.duration;

    // Configure axes selection
    conf.cfg.any_motion.select_x = config.selectX ? BMI2_ENABLE : BMI2_DISABLE;
    conf.cfg.any_motion.select_y = config.selectY ? BMI2_ENABLE : BMI2_DISABLE;
    conf.cfg.any_motion.select_z = config.selectZ ? BMI2_ENABLE : BMI2_DISABLE;

    return bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);
}

BMI2_INTF_RETURN_TYPE BMI270::applyNoMotionConfig(const MotionConfig& config)
{
    struct bmi2_sens_config conf {};
    conf.type = BMI2_NO_MOTION;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    // Clamp threshold to 0-1000 mg, convert to BMI270 units
    // BMI270 threshold is 11-bit value: threshold = (threshold_mg * 2^11) / 1000
    uint16_t thresholdClamped = (config.threshold > 1000) ? 1000 : config.threshold;
    conf.cfg.no_motion.threshold = (thresholdClamped * 2048) / 1000;

    // Clamp duration to 0-163 samples (8-bit value 0-163)
    conf.cfg.no_motion.duration = (config.duration > 163) ? 163 : config.duration;

    // Configure axes selection
    conf.cfg.no_motion.select_x = config.selectX ? BMI2_ENABLE : BMI2_DISABLE;
    conf.cfg.no_motion.select_y = config.selectY ? BMI2_ENABLE : BMI2_DISABLE;
    conf.cfg.no_motion.select_z = config.selectZ ? BMI2_ENABLE : BMI2_DISABLE;

    return bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);
}

BMI2_INTF_RETURN_TYPE BMI270::applyWristMotionConfig(const WristMotionConfig& config)
{
    struct bmi2_sens_config conf {};
    conf.type = BMI2_WRIST_WEAR_WAKE_UP;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    conf.cfg.wrist_wear_wake_up.min_angle_focus    = config.min_angle_focus;
    conf.cfg.wrist_wear_wake_up.min_angle_nonfocus = config.min_angle_nonfocus;
    conf.cfg.wrist_wear_wake_up.max_tilt_lr        = config.max_tilt_lr;
    conf.cfg.wrist_wear_wake_up.max_tilt_ll        = config.max_tilt_ll;
    conf.cfg.wrist_wear_wake_up.max_tilt_pd        = config.max_tilt_pd;
    conf.cfg.wrist_wear_wake_up.max_tilt_pu        = config.max_tilt_pu;

    LOG_DEBUG("WRIST_MOTION is configured\n");

    return bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);
}

/**
 * @brief Enable a feature sensor in hardware.
 * @param sensor: Feature sensor type.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::enableFeatureHardware(SensorType sensor)
{
    uint8_t bmi2Type = sensorTypeToBmi2(sensor);
    if (bmi2Type == 0xFF) {
        return BMI2_E_INVALID_SENSOR;
    }

    LOG_DEBUG("Enabling feature: sensor=%d, bmi2Type=%d\n",
              static_cast<int>(sensor), bmi2Type);

    uint8_t sens_list[] = {bmi2Type};
    BMI2_INTF_RETURN_TYPE rslt = bmi270_sensor_enable(sens_list, 1, &mBmi2Dev);

    if (rslt == BMI2_OK) {
        mFeatureEngineEnabled = true;
        LOG_DEBUG("Feature enabled successfully\n");
    } else {
        LOG_ERROR("Failed to enable feature, error %d\n", rslt);
    }

    return rslt;
}

/**
 * @brief Disable a feature sensor in hardware.
 * @param sensor: Feature sensor type.
 * @return BMI2_OK on success.
 */
BMI2_INTF_RETURN_TYPE BMI270::disableFeatureHardware(SensorType sensor)
{
    uint8_t bmi2Type = sensorTypeToBmi2(sensor);
    if (bmi2Type == 0xFF) {
        return BMI2_E_INVALID_SENSOR;
    }

    uint8_t sens_list[] = {bmi2Type};
    return bmi270_sensor_disable(sens_list, 1, &mBmi2Dev);
}

/*******************************************************************************
 * Batch Operations for Multiple Sensors
 ******************************************************************************/

/**
 * @brief Enable multiple sensors at once (more efficient).
 * @param sensors: Array of sensor types.
 * @param configs: Array of configurations (same order as sensors).
 * @param count: Number of sensors.
 * @return True if all sensors enabled successfully.
 */
bool BMI270::requestSensorBatch(const SensorType* sensors,
                              const SensorConfig* configs,
                              uint8_t count)
{
    if (count == 0 || count > static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        return false;
    }

    // Lazy init
    if (!mChipInitialized) {
        if (!initChip()) {
            return false;
        }
    }

    // Collect BMI2 sensor types for batch enable
    uint8_t bmi2Sensors[16];
    uint8_t bmi2Count = 0;

    // First pass: apply all configurations
    for (uint8_t i = 0; i < count; i++) {
        uint8_t sensorIndex = static_cast<uint8_t>(sensors[i]);
        mSensorConfigs[sensorIndex] = configs[i];
        mSensorEnabled[sensorIndex] = true;

        // Handle accelerometer dependency
        if (sensorRequiresAccel(sensors[i])) {
            mAccelRefCount++;
        }

        // Apply config
        BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;
        switch (sensors[i]) {
            case SensorType::ACCELEROMETER:
                rslt = applyAccelConfig(configs[i]);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_ACCEL;
                }
                break;

            case SensorType::GYROSCOPE:
                rslt = applyGyroConfig(configs[i]);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_GYRO;
                }
                break;

            case SensorType::STEP_COUNTER:
                rslt = applyStepCounterConfig(configs[i]);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_STEP_COUNTER;
                }
                break;

            case SensorType::ANY_MOTION:
                rslt = applyAnyMotionConfig(mAnyMotionConfig);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_ANY_MOTION;
                }
                break;

            case SensorType::NO_MOTION:
                rslt = applyNoMotionConfig(mNoMotionConfig);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_NO_MOTION;
                }
                break;

            case SensorType::WRIST_WAKEUP:
                rslt = applyWristMotionConfig(mWristMotionConfig);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_WRIST_WEAR_WAKE_UP;
                }
                break;

            case SensorType::WRIST_GESTURE:
                rslt = applyWristGestureConfig(mWristGestureConfig);
                if (rslt == BMI2_OK) {
                    bmi2Sensors[bmi2Count++] = BMI2_WRIST_GESTURE;
                }
                break;

            default:
                if (isFeatureSensor(sensors[i])) {
                    uint8_t bmi2Type = sensorTypeToBmi2(sensors[i]);
                    if (bmi2Type != 0xFF) {
                        bmi2Sensors[bmi2Count++] = bmi2Type;
                    }
                }
                break;
        }

        if (rslt != BMI2_OK) {
            LOG_ERROR("Failed to configure sensor %d\n", sensorIndex);
            // Continue with others but log error
        }
    }

    // Check if we need to add accelerometer for dependencies
    if (mAccelRefCount > 0 &&
        !mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)]) {
        // Find if ACCEL already in list
        bool accelInList = false;
        for (uint8_t i = 0; i < bmi2Count; i++) {
            if (bmi2Sensors[i] == BMI2_ACCEL) {
                accelInList = true;
                break;
            }
        }
        if (!accelInList) {
            enableAccelInternal();
        }
    }

    // Batch enable all sensors
    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    // Separate basic sensors and features
    uint8_t basicSensors[2];
    uint8_t basicCount = 0;
    uint8_t featureSensors[10];
    uint8_t featureCount = 0;

    for (uint8_t i = 0; i < bmi2Count; i++) {
        if (bmi2Sensors[i] == BMI2_ACCEL || bmi2Sensors[i] == BMI2_GYRO) {
            basicSensors[basicCount++] = bmi2Sensors[i];
        } else {
            featureSensors[featureCount++] = bmi2Sensors[i];
        }
    }

    if (basicCount > 0) {
        rslt = bmi2_sensor_enable(basicSensors, basicCount, &mBmi2Dev);
        if (rslt != BMI2_OK) {
            LOG_ERROR("Failed to enable basic sensors: %d\n", rslt);
        }
    }

    if (featureCount > 0) {
        rslt = bmi270_sensor_enable(featureSensors, featureCount, &mBmi2Dev);
        if (rslt == BMI2_OK) {
            mFeatureEngineEnabled = true;
        } else {
            LOG_ERROR("Failed to enable feature sensors: %d\n", rslt);
        }
    }

    updateScalingFactors();
    LOG_DEBUG("Batch enabled %d sensors\n", count);
    return (rslt == BMI2_OK);
}

/**
 * @brief Release multiple sensors at once.
 * @param sensors: Array of sensor types to release.
 * @param count: Number of sensors.
 * @return True if all sensors released successfully.
 */
bool BMI270::releaseSensorBatch(const SensorType* sensors, uint8_t count)
{
    if (count == 0) {
        return true;
    }

    // Collect BMI2 sensor types for batch disable
    uint8_t basicSensors[2];
    uint8_t basicCount = 0;
    uint8_t featureSensors[10];
    uint8_t featureCount = 0;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t sensorIndex = static_cast<uint8_t>(sensors[i]);

        if (!mSensorEnabled[sensorIndex]) {
            continue;
        }

        mSensorEnabled[sensorIndex] = false;

        // Update reference counting
        if (sensorRequiresAccel(sensors[i])) {
            if (mAccelRefCount > 0) {
                mAccelRefCount--;
            }
        }

        uint8_t bmi2Type = sensorTypeToBmi2(sensors[i]);
        if (bmi2Type == 0xFF) {
            continue;
        }

        if (bmi2Type == BMI2_ACCEL || bmi2Type == BMI2_GYRO) {
            // Only add to disable list if no dependencies
            if (bmi2Type == BMI2_ACCEL && mAccelRefCount > 0) {
                continue;
            }
            basicSensors[basicCount++] = bmi2Type;
        } else {
            featureSensors[featureCount++] = bmi2Type;
        }
    }

    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    if (basicCount > 0) {
        rslt = bmi2_sensor_disable(basicSensors, basicCount, &mBmi2Dev);
    }

    if (featureCount > 0) {
        rslt = bmi270_sensor_disable(featureSensors, featureCount, &mBmi2Dev);
    }

    // Check for lazy deinit
    if (shouldDeinitChip()) {
        deinitChip();
    }

    LOG_DEBUG("Batch released %d sensors\n", count);
    return (rslt == BMI2_OK);
}

/*******************************************************************************
 * Query Functions
 ******************************************************************************/

/**
 * @brief Check if a specific sensor is currently enabled.
 * @param sensor: Sensor type to check.
 * @return True if sensor is enabled.
 */
bool BMI270::isSensorEnabled(SensorType sensor) const
{
    uint8_t sensorIndex = static_cast<uint8_t>(sensor);
    if (sensorIndex >= static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        return false;
    }
    return mSensorEnabled[sensorIndex];
}

/**
 * @brief Get bitmask of all enabled sensors.
 * @return Bitmask where bit N corresponds to SensorType N.
 */
uint16_t BMI270::getEnabledSensorsMask() const
{
    uint16_t mask = 0;
    for (int i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        if (mSensorEnabled[i]) {
            mask |= (1 << i);
        }
    }
    return mask;
}

/**
 * @brief Get current sensor configuration.
 * @param sensor: Sensor type.
 * @return Current configuration.
 */
BMI270::SensorConfig BMI270::getSensorConfig(SensorType sensor) const
{
    uint8_t sensorIndex = static_cast<uint8_t>(sensor);
    if (sensorIndex < static_cast<uint8_t>(SensorType::MAX_SENSORS)) {
        return mSensorConfigs[sensorIndex];
    }
    return SensorConfig{};
}

/**
 * @brief Release all sensors and deinitialize chip.
 * @return True on success.
 */
bool BMI270::releaseAll()
{
    for (int i = 0; i < static_cast<uint8_t>(SensorType::MAX_SENSORS); i++) {
        if (mSensorEnabled[i]) {
            releaseSensor(static_cast<SensorType>(i));
        }
    }

    if (mChipInitialized) {
        deinitChip();
    }

    return true;
}

/*******************************************************************************
 * Unit Conversion Utilities
 ******************************************************************************/

void BMI270::updateScalingFactors()
{
    uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);
    mAccelScale = getAccelScale(static_cast<AccelRange>(mSensorConfigs[accelIndex].range));

    uint8_t gyroIndex = static_cast<uint8_t>(SensorType::GYROSCOPE);
    mGyroScale = getGyroScale(static_cast<GyroRange>(mSensorConfigs[gyroIndex].range));
}

float BMI270::getAccelScale(AccelRange range) const
{
    float scale = 1.0f;
    switch (range) {
        case AccelRange::RANGE_2G:  scale = 2.0f * 9.80665f / 32768.0f; break;
        case AccelRange::RANGE_4G:  scale = 4.0f * 9.80665f / 32768.0f; break;
        case AccelRange::RANGE_8G:  scale = 8.0f * 9.80665f / 32768.0f; break;
        case AccelRange::RANGE_16G: scale = 16.0f * 9.80665f / 32768.0f; break;
        default: scale = 2.0f * 9.80665f / 32768.0f; break;
    }

    return scale;
}

float BMI270::getGyroScale(GyroRange range) const
{
    float scale = 1.0f;
    constexpr float kPi = 3.14159265358979323846f;
    switch (range) {
        case GyroRange::RANGE_125DPS:  scale = 125.0f * kPi / (180.0f * 32768.0f); break;
        case GyroRange::RANGE_250DPS:  scale = 250.0f * kPi / (180.0f * 32768.0f); break;
        case GyroRange::RANGE_500DPS:  scale = 500.0f * kPi / (180.0f * 32768.0f); break;
        case GyroRange::RANGE_1000DPS: scale = 1000.0f * kPi / (180.0f * 32768.0f); break;
        case GyroRange::RANGE_2000DPS: scale = 2000.0f * kPi / (180.0f * 32768.0f); break;
        default:   scale = 250.0f * kPi / (180.0f * 32768.0f); break;
    }

    return scale;
}

float BMI270::rawToG(int16_t raw) const
{
    return raw * mAccelScale / 9.80665f;
}

float BMI270::rawToMs2(int16_t raw) const
{
    return raw * mAccelScale;
}

float BMI270::rawToDps(int16_t raw) const
{
    return raw * mGyroScale * 180.0f / 3.14159265358979323846f;
}

float BMI270::rawToRads(int16_t raw) const
{
    return raw * mGyroScale;
}

uint8_t BMI270::accelOdrFor(uint32_t periodUs) const
{
    // mAccelRefCount counts the enabled features that run on this data.
    return Bmi270Odr::accelFromPeriodUs(periodUs, mAccelRefCount > 0, kFeaturePeriodUs);
}

bool BMI270::retuneAccel()
{
    const uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);

    // Brought up for the features alone, it runs at their rate already.
    if (!mSensorEnabled[accelIndex]) {
        return false;
    }

    if (accelOdrFor(mSensorConfigs[accelIndex].periodUs) == mAccelOdr) {
        return false;
    }

    // What the FIFO holds was taken at the old rate.
    if (mFifoEnabled) {
        processFifo();
    }

    if (applyAccelConfig(mSensorConfigs[accelIndex]) != BMI2_OK) {
        LOG_ERROR("Failed to retune the accelerometer\n");
        return false;
    }

    LOG_DEBUG("Accel period now %lu us\n", static_cast<unsigned long>(Bmi270Odr::periodUs(mAccelOdr)));
    return true;
}

uint32_t BMI270::getAppliedPeriodUs(SensorType sensor) const
{
    if (sensor == SensorType::ACCELEROMETER) {
        return Bmi270Odr::periodUs(mAccelGrid.odr());
    }

    if (sensor == SensorType::GYROSCOPE) {
        return Bmi270Odr::periodUs(mGyroGrid.odr());
    }

    return 0;
}

uint32_t BMI270::getSensorTime()
{
    uint8_t data[3] = {0};
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_regs(BMI2_SENSORTIME_ADDR, data, 3, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return 0;
    }
    return (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) |
           data[0];
}

/**
 * @brief Get system time in microseconds.
 * @return System time in microseconds (uint64_t).
 *
 * Uses callback if set, otherwise falls back to Driver::System::GetTimeMs().
 */
uint64_t BMI270::getSystemTime()
{
    if (mGetSystemTime) {
        return mGetSystemTime();
    }
    // Fallback to Driver::System if callback not set
    return static_cast<uint64_t>(Driver::System::GetTimeMs()) * 1000;
}

uint32_t BMI270::subtractWithOverflow(uint32_t timestamp, uint32_t ticks) const
{
    return (timestamp - ticks) & 0xFFFFFF;
}

uint64_t BMI270::ticksToUs(uint32_t ticks) const
{
    return (uint64_t)ticks * 625ull >> 4;
}

/*******************************************************************************
 * Direct Data Reading
 ******************************************************************************/

bool BMI270::readAccelerometer(SensorData& data)
{
    if (!mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)]) {
        return false;
    }

    struct bmi2_sens_data sensorData;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_sensor_data(&sensorData, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    data.x = sensorData.acc.x * mAccelScale;
    data.y = sensorData.acc.y * mAccelScale;
    data.z = sensorData.acc.z * mAccelScale;
    data.timestamp = getSensorTime();

    return true;
}

bool BMI270::readAccelerometerRaw(SensorRawData& data)
{
    if (!mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)]) {
        return false;
    }

    struct bmi2_sens_data sensorData;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_sensor_data(&sensorData, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    data.x = sensorData.acc.x;
    data.y = sensorData.acc.y;
    data.z = sensorData.acc.z;
    data.sensorTime = getSensorTime();

    return true;
}

bool BMI270::readGyroscope(SensorData& data)
{
    if (!mSensorEnabled[static_cast<uint8_t>(SensorType::GYROSCOPE)]) {
        return false;
    }

    struct bmi2_sens_data sensorData;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_sensor_data(&sensorData, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    data.x = sensorData.gyr.x * mGyroScale;
    data.y = sensorData.gyr.y * mGyroScale;
    data.z = sensorData.gyr.z * mGyroScale;
    data.timestamp = getSensorTime();

    return true;
}

bool BMI270::readTemperature(float& temperature)
{
    uint8_t tempData[2] = {0};
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_regs(BMI2_TEMPERATURE_0_ADDR, tempData, 2, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    int16_t tempRaw = static_cast<int16_t>((tempData[1] << 8) | tempData[0]);

    // Check for invalid value (0x8000) - sensor not ready yet
    if (tempRaw == static_cast<int16_t>(0x8000)) {
        return false;
    }

    temperature = (static_cast<float>(tempRaw) / 512.0f) + 23.0f;
    return true;
}

bool BMI270::readStepCounter(StepData& data)
{
    struct bmi2_feat_sensor_data featData{};
    featData.type = BMI2_STEP_COUNTER;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_feature_data(&featData, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    data.steps = featData.sens_data.step_counter_output;
    data.timestamp = getSensorTime();

    return true;
}

bool BMI270::readActivity(ActivityData& data)
{
    if (!mSensorEnabled[static_cast<uint8_t>(SensorType::ACTIVITY_RECOGNITION)]) {
        return false;
    }

    struct bmi2_feat_sensor_data featData{};
    featData.type = BMI2_STEP_ACTIVITY;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_feature_data(&featData, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    // Convert BMI270 activity output to ActivityType enum
    uint8_t activityRaw = featData.sens_data.activity_output;
    if (activityRaw <= static_cast<uint8_t>(ActivityType::UNKNOWN)) {
        data.activity = static_cast<ActivityType>(activityRaw);
    } else {
        data.activity = ActivityType::UNKNOWN;
    }
    data.timestamp = getSensorTime();

    return true;
}

bool BMI270::resetStepCounter()
{
    if (!mFeatureEngineEnabled) {
        LOG_ERROR("Feature engine not enabled\n");
        return false;
    }

    struct bmi2_sens_config conf;
    conf.type = BMI2_STEP_COUNTER;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    conf.cfg.step_counter.reset_counter = 1;
    rslt = bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);

    return (rslt == BMI2_OK);
}

bool BMI270::configureAnyMotion(const MotionConfig& config)
{
    mAnyMotionConfig = config;

    LOG_DEBUG("ANY_MOTION config stored: thresh=%dmg, dur=%d, axes=%d%d%d\n",
              config.threshold, config.duration,
              config.selectX, config.selectY, config.selectZ);

    return true;
}

bool BMI270::configureNoMotion(const MotionConfig& config)
{
    mNoMotionConfig = config;

    LOG_DEBUG("NO_MOTION config stored: thresh=%dmg, dur=%d, axes=%d%d%d\n",
              config.threshold, config.duration,
              config.selectX, config.selectY, config.selectZ);

    return true;
}

bool BMI270::configureWristGesture(const WristGestureConfig& config)
{
    mWristGestureConfig = config;

    LOG_DEBUG("WRIST_GESTURE config stored: arm=%d, flick_peak=%d, flick_smp=%d, max_dur=%d\n",
              config.wearable_arm, config.min_flick_peak,
              config.min_flick_samples, config.max_duration);

    return true;
}

bool BMI270::configureWristWakeup(const WristMotionConfig& config)
{
    mWristMotionConfig = config;

    LOG_DEBUG("WRIST_WAKEUP config stored: focus=%d, nonfocus=%d, lr=%d, ll=%d, pd=%d, pu=%d\n",
              config.min_angle_focus, config.min_angle_nonfocus,
              config.max_tilt_lr, config.max_tilt_ll,
              config.max_tilt_pd, config.max_tilt_pu);

    return true;
}

BMI2_INTF_RETURN_TYPE BMI270::applyWristGestureConfig(const WristGestureConfig& config)
{
    struct bmi2_sens_config conf{};
    conf.type = BMI2_WRIST_GESTURE;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_sensor_config(&conf, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return rslt;
    }

    conf.cfg.wrist_gest.wearable_arm      = config.wearable_arm;
    conf.cfg.wrist_gest.min_flick_peak    = config.min_flick_peak;
    conf.cfg.wrist_gest.min_flick_samples = config.min_flick_samples;
    conf.cfg.wrist_gest.max_duration      = config.max_duration;

    LOG_DEBUG("WRIST_GESTURE is configured\n");

    return bmi270_set_sensor_config(&conf, 1, &mBmi2Dev);
}

bool BMI270::readWristGesture(WristGestureData& data)
{
    if (!mSensorEnabled[static_cast<uint8_t>(SensorType::WRIST_GESTURE)]) {
        return false;
    }

    struct bmi2_feat_sensor_data featData{};
    featData.type = BMI2_WRIST_GESTURE;

    BMI2_INTF_RETURN_TYPE rslt = bmi270_get_feature_data(&featData, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    uint8_t raw = featData.sens_data.wrist_gesture_output;

    // The WR_GEST_OUT register holds the last detected gesture permanently,
    // even after the feature is disabled and re-enabled. Return false if the
    // value has not changed from the baseline captured at enable time.
    if (raw == mWristGestureBaseline) {
        return false;
    }

    // Reset baseline to UNKNOWN (0) instead of storing raw value.
    // The sensor briefly returns the register to 0 between gestures, so the
    // sequence for a repeated gesture becomes: 0 -> N -> 0 -> N.
    // Storing raw would block the second identical gesture since the register
    // would never differ from the stored value until a different gesture fires.
    mWristGestureBaseline = 0;

    data.gesture   = (raw <= static_cast<uint8_t>(WristGestureType::FLICK_OUT))
                         ? static_cast<WristGestureType>(raw)
                         : WristGestureType::UNKNOWN;
    data.timestamp = getSensorTime();

    return true;
}

/*******************************************************************************
 * Interrupt Configuration
 ******************************************************************************/

bool BMI270::configureInterruptPin(InterruptPin pin, const InterruptConfig& config)
{
    struct bmi2_int_pin_config pinConfig{};
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_int_pin_config(&pinConfig, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    uint8_t pinIndex = (pin == InterruptPin::INT1) ? 0 : 1;
    pinConfig.pin_type = (pin == InterruptPin::INT1) ? BMI2_INT1 : BMI2_INT2;
    pinConfig.pin_cfg[pinIndex].input_en = BMI2_INT_INPUT_DISABLE;
    pinConfig.pin_cfg[pinIndex].lvl = config.activeHigh ? BMI2_INT_ACTIVE_HIGH : BMI2_INT_ACTIVE_LOW;
    pinConfig.pin_cfg[pinIndex].od = config.pushPull ? BMI2_INT_PUSH_PULL : BMI2_INT_OPEN_DRAIN;
    pinConfig.pin_cfg[pinIndex].output_en = config.outputEnabled ? BMI2_INT_OUTPUT_ENABLE : BMI2_INT_OUTPUT_DISABLE;
    pinConfig.int_latch = config.latched ? BMI2_INT_LATCH : BMI2_INT_NON_LATCH;

    rslt = bmi2_set_int_pin_config(&pinConfig, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    // Store config and mark as applied
    if (pin == InterruptPin::INT1) {
        mInt1Config = config;
        mInt1Configured = true;
    } else {
        mInt2Config = config;
        mInt2Configured = true;
    }

    return true;
}

void BMI270::setInterruptConfig(InterruptPin pin, const InterruptConfig& config)
{
    if (pin == InterruptPin::INT1) {
        mInt1Config = config;
        mInt1Configured = false;  // Mark as not yet applied to hardware
    } else {
        mInt2Config = config;
        mInt2Configured = false;
    }
}

bool BMI270::mapSensorInterrupt(SensorType sensor, InterruptPin pin)
{
    struct bmi2_sens_int_config sensInt;
    sensInt.hw_int_pin = (pin == InterruptPin::INT1) ? BMI2_INT1 : BMI2_INT2;

    switch (sensor) {
        case SensorType::STEP_COUNTER:
            sensInt.type = BMI2_STEP_COUNTER;
            break;
        case SensorType::STEP_DETECTOR:
            sensInt.type = BMI2_STEP_DETECTOR;
            break;
        case SensorType::ACTIVITY_RECOGNITION:
            sensInt.type = BMI2_STEP_ACTIVITY;
            break;
        case SensorType::NO_MOTION:
            sensInt.type = BMI2_NO_MOTION;
            break;
        case SensorType::ANY_MOTION:
            sensInt.type = BMI2_ANY_MOTION;
            break;
        case SensorType::SIG_MOTION:
            sensInt.type = BMI2_SIG_MOTION;
            break;
        case SensorType::WRIST_WAKEUP:
            sensInt.type = BMI2_WRIST_WEAR_WAKE_UP;
            break;
        case SensorType::WRIST_GESTURE:
            sensInt.type = BMI2_WRIST_GESTURE;
            break;
        default:
            LOG_ERROR("Unknown sensor type %d for interrupt mapping\n", static_cast<uint8_t>(sensor));
            return false;
    }

    LOG_DEBUG("Mapping sensor %d (BMI2 type %d) to INT%d\n",
              static_cast<uint8_t>(sensor), sensInt.type,
              (pin == InterruptPin::INT1) ? 1 : 2);

    BMI2_INTF_RETURN_TYPE rslt = bmi270_map_feat_int(&sensInt, 1, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to map feature interrupt, error %d\n", rslt);
        return false;
    }

    LOG_DEBUG("Feature interrupt mapped successfully\n");

    // Auto-apply interrupt pin config if stored and not yet applied
    if (pin == InterruptPin::INT1) {
        if (mInt1Config.outputEnabled && !mInt1Configured) {
            if (!configureInterruptPin(InterruptPin::INT1, mInt1Config)) {
                LOG_WARNING("Failed to auto-configure INT1 pin\n");
            }
        }
    } else {
        if (mInt2Config.outputEnabled && !mInt2Configured) {
            if (!configureInterruptPin(InterruptPin::INT2, mInt2Config)) {
                LOG_WARNING("Failed to auto-configure INT2 pin\n");
            }
        }
    }

    return true;
}

bool BMI270::mapDataReadyInterrupt(bool accel, bool gyro, InterruptPin pin)
{
    uint8_t dataInt = 0;
    if (accel) dataInt |= BMI2_DRDY_INT;
    // Note: BMI270 maps both ACC and GYR DRDY together

    enum bmi2_hw_int_pin hwPin = (pin == InterruptPin::INT1) ? BMI2_INT1 : BMI2_INT2;
    return (bmi2_map_data_int(dataInt, hwPin, &mBmi2Dev) == BMI2_OK);
}

bool BMI270::readInterruptStatus(uint16_t& status)
{
    return (bmi2_get_int_status(&status, &mBmi2Dev) == BMI2_OK);
}

void BMI270::processInterrupt()
{
    // Get system timestamp at interrupt time (in microseconds)
    uint64_t timestamp = getSystemTime();

    // Reading status clears latched interrupt
    uint16_t intStatus = 0;
    if (!readInterruptStatus(intStatus)) {
        return;
    }

    // Debug: log non-zero interrupt status
    if (intStatus != 0) {
        //LOG_DEBUG("INT status: 0x%04X\n", intStatus);
    }

    // FIFO watermark
    const bool fifoRead = (intStatus & BMI2_FWM_INT_STATUS_MASK) != 0;

    if (fifoRead) {
        processFifo();
        // Clear lingering FWM and capture any new events
        uint16_t newStatus = 0;
        readInterruptStatus(newStatus);

        // Merge: keep unprocessed original flags + add all new flags
        intStatus = (intStatus & ~BMI2_FWM_INT_STATUS_MASK) | newStatus;
    }

    // FIFO full: the part has dropped its oldest frames to take the newest.
    // What it holds is as good as any other read, so it is read, not thrown
    // away.
    if (intStatus & BMI2_FFULL_INT_STATUS_MASK) {
        LOG_WARNING("FIFO full\n");

        if (!fifoRead) {
            processFifo();
        }
    }

    // Step counter
    if (intStatus & BMI270_STEP_CNT_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::STEP_COUNTER)]) {
            StepData stepData;
            if (readStepCounter(stepData)) {
                if (stepData.steps != mPrevStepCount) {
                    mPrevStepCount = stepData.steps;
                    stepData.timestamp = timestamp;
                    mpListener->onStepCounter(stepData);
                }
            }
        }

        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::STEP_DETECTOR)]) {
            mpListener->onStepDetector(timestamp);
        }
    }

    // Activity
    if (intStatus & BMI270_STEP_ACT_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::ACTIVITY_RECOGNITION)]) {
            ActivityData activityData;
            if (readActivity(activityData)) {
                activityData.timestamp = timestamp;
                mpListener->onActivity(activityData);
            }
        }
    }

    // Any motion
    if (intStatus & BMI270_ANY_MOT_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::ANY_MOTION)]) {
            mpListener->onMotion(timestamp);
        }
    }

    // No motion
    if (intStatus & BMI270_NO_MOT_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::NO_MOTION)]) {
            mpListener->onNoMotion(timestamp);
        }
    }

    // Significant motion
    if (intStatus & BMI270_SIG_MOT_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::SIG_MOTION)]) {
            mpListener->onSigMotion(timestamp);
        }
    }

    // Wrist wakeup
    if (intStatus & BMI270_WRIST_WAKE_UP_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::WRIST_WAKEUP)]) {
            mpListener->onWristWakeup(timestamp);
        }
    }

    // Wrist gesture
    if (intStatus & BMI270_WRIST_GEST_STATUS_MASK) {
        if (mpListener && mSensorEnabled[static_cast<uint8_t>(SensorType::WRIST_GESTURE)]) {
            WristGestureData gestData;
            if (readWristGesture(gestData)) {
                gestData.timestamp = timestamp;
                mpListener->onWristGesture(gestData);
            }
        }
    }
}

/*******************************************************************************
 * Power Management
 ******************************************************************************/

bool BMI270::setAdvancedPowerSave(bool enable)
{
    BMI2_INTF_RETURN_TYPE rslt = bmi2_set_adv_power_save(
        enable ? BMI2_ENABLE : BMI2_DISABLE, &mBmi2Dev);
    return (rslt == BMI2_OK);
}

bool BMI270::softReset()
{
    BMI2_INTF_RETURN_TYPE rslt = bmi2_set_command_register(BMI2_SOFT_RESET_CMD, &mBmi2Dev);
    if (rslt == BMI2_OK) {
        forgetChipState();
    }
    OS::Delay(10);
    return (rslt == BMI2_OK);
}

bool BMI270::selfTest()
{
    if (!mIsConfigured) {
        return false;
    }

    // The self-test scales its result by the resolution bmi270_init() sets,
    // and its closing soft reset reloads the config file that call selects.
    if (!initChip()) {
        return false;
    }

    // The test sets the accelerometer to 1600 Hz / 16 g and soft-resets the
    // part once it has read both polarities, whether they pass or not. Either
    // way nothing configured before it still holds, so the next requestSensor()
    // starts again from initChip().
    BMI2_INTF_RETURN_TYPE rslt = bmi2_perform_accel_self_test(&mBmi2Dev);

    // Any other error can stop it short of that reset, with the accelerometer
    // still running, so reset it here. softReset() forgets the state only if
    // its reset goes through; otherwise the state stays, and a releaseAll()
    // still reaches deinitChip() and tries again.
    if (rslt == BMI2_OK || rslt == BMI2_E_SELF_TEST_FAIL) {
        forgetChipState();
    } else {
        softReset();
    }

    if (rslt != BMI2_OK) {
        LOG_ERROR("Accelerometer self-test failed\n");
        return false;
    }

    LOG_INFO("Self-test passed\n");
    return true;
}

/*******************************************************************************
 * FIFO Operations
 ******************************************************************************/

bool BMI270::configureFifo(const FifoConfig& config)
{
    // Nothing but the level is new: the FIFO runs on. Switching it off and
    // on would lose what the part stores in the meantime, and with it the
    // frame that marks a rate change just made.
    if (mFifoEnabled
        && config.accelEnabled == mFifoConfig.accelEnabled
        && config.gyroEnabled == mFifoConfig.gyroEnabled
        && config.auxEnabled == mFifoConfig.auxEnabled
        && config.headerEnabled == mFifoConfig.headerEnabled
        && config.sensorTimeEnabled == mFifoConfig.sensorTimeEnabled
        && config.interruptPin == mFifoConfig.interruptPin) {
        return (config.watermarkBytes == mFifoConfig.watermarkBytes) || setFifoWatermark(config.watermarkBytes);
    }

    BMI2_INTF_RETURN_TYPE rslt = BMI2_OK;

    // Build 16-bit config value
    // Bits 0-7: FIFO_CONFIG_0 (fifo_stop_on_full=bit0, fifo_time_en=bit1)
    // Bits 8-15: FIFO_CONFIG_1 (acc_en, gyr_en, header_en, etc)
    uint16_t fifoConfig = 0;

    // FIFO_CONFIG_0 bits
    if (config.sensorTimeEnabled) fifoConfig |= BMI2_FIFO_TIME_EN;

    // FIFO_CONFIG_1 bits (shifted to upper byte)
    if (config.accelEnabled) fifoConfig |= BMI2_FIFO_ACC_EN;
    if (config.gyroEnabled) fifoConfig |= BMI2_FIFO_GYR_EN;
    if (config.auxEnabled) fifoConfig |= BMI2_FIFO_AUX_EN;
    if (config.headerEnabled) fifoConfig |= BMI2_FIFO_HEADER_EN;

    // Switch off only what is not wanted. A stream that stays in the FIFO
    // loses no frame to another's coming or going.
    const uint16_t all = BMI2_FIFO_ALL_EN | BMI2_FIFO_STOP_ON_FULL | BMI2_FIFO_TIME_EN;

    rslt = bmi2_set_fifo_config(static_cast<uint16_t>(all & ~fifoConfig), BMI2_DISABLE, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    rslt = bmi2_set_fifo_config(fifoConfig, BMI2_ENABLE, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    // Verify FIFO configuration was written correctly
    uint16_t readConfig = 0;
    rslt = bmi2_get_fifo_config(&readConfig, &mBmi2Dev);
    if (rslt == BMI2_OK) {
        LOG_DEBUG("FIFO config: wrote=0x%04X, read=0x%04X\n", fifoConfig, readConfig);
    }

    // Also read raw register to double-check
    uint8_t regData[2];
    rslt = bmi2_get_regs(BMI2_FIFO_CONFIG_0_ADDR, regData, 2, &mBmi2Dev);
    if (rslt == BMI2_OK) {
        LOG_DEBUG("FIFO_CONFIG_0=0x%02X, FIFO_CONFIG_1=0x%02X (acc_en=%d, gyr_en=%d, aux_en=%d, header_en=%d, time_en=%d)\n",
                  regData[0], regData[1],
                  (regData[1] >> 6) & 1,  // bit 6 of CONFIG_1
                  (regData[1] >> 7) & 1,  // bit 7 of CONFIG_1
                  (regData[1] >> 5) & 1,  // bit 5 of CONFIG_1
                  (regData[1] >> 4) & 1,  // bit 4 of CONFIG_1
                  (regData[0] >> 1) & 1); // bit 1 of CONFIG_0
    }

    // Setup FIFO frame
    mFifoFrame.data = mFifoBuffer;
    mFifoFrame.length = kFifoBufferSize;

    // Set watermark
    rslt = bmi2_set_fifo_wm(config.watermarkBytes, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        LOG_ERROR("Failed to set FIFO watermark: rslt=%d\n", rslt);
        return false;
    }

    // Map watermark interrupt
    enum bmi2_hw_int_pin hwPin = (config.interruptPin == InterruptPin::INT1) ? BMI2_INT1 : BMI2_INT2;
    rslt = bmi2_map_data_int(BMI2_FWM_INT, hwPin, &mBmi2Dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    // Auto-apply interrupt pin config if stored and not yet applied
    if (config.interruptPin == InterruptPin::INT1) {
        if (mInt1Config.outputEnabled && !mInt1Configured) {
            if (!configureInterruptPin(InterruptPin::INT1, mInt1Config)) {
                LOG_WARNING("Failed to auto-configure INT1 pin\n");
            }
        }
    } else {
        if (mInt2Config.outputEnabled && !mInt2Configured) {
            if (!configureInterruptPin(InterruptPin::INT2, mInt2Config)) {
                LOG_WARNING("Failed to auto-configure INT2 pin\n");
            }
        }
    }

    // A stream that enters the FIFO starts anew: where it stood when it was
    // last in is no guide to where the counter is by now, and whatever the
    // part said of its rate meanwhile is lost, so it is taken to run at the
    // rate written. One that stays in runs on, a rate change it still waits
    // for included.
    const bool accelEnters = config.accelEnabled && !(mFifoEnabled && mFifoConfig.accelEnabled);
    const bool gyroEnters  = config.gyroEnabled && !(mFifoEnabled && mFifoConfig.gyroEnabled);

    if (accelEnters) {
        mAccelGrid.restart();
        mAccelGrid.setOdr(mAccelOdr);
    }

    if (gyroEnters) {
        mGyroGrid.restart();
        mGyroGrid.setOdr(mGyroOdr);
    }

    mFifoEnabled = true;
    LOG_DEBUG("FIFO configured: acc=%d, gyro=%d, aux=%d, header=%d, time=%d, wm=%d\n",
             config.accelEnabled, config.gyroEnabled, config.auxEnabled,
             config.headerEnabled, config.sensorTimeEnabled, config.watermarkBytes);

    // Save current configuration
    mFifoConfig = config;

    return true;
}

bool BMI270::disableFifo()
{
    BMI2_INTF_RETURN_TYPE rslt = bmi2_set_fifo_config(
            BMI2_FIFO_ALL_EN | BMI2_FIFO_STOP_ON_FULL | BMI2_FIFO_TIME_EN/* |
            BMI2_FIFO_TAG_INT1 | BMI2_FIFO_TAG_INT2*/, BMI2_DISABLE, &mBmi2Dev);
    mFifoEnabled = false;
    mFifoConfig = {};
    return (rslt == BMI2_OK);
}

/*
 * @brief Process FIFO data and trigger appropriate callbacks
 *
 * Extracts sensors separately, calls onFifoAccel() and onFifoGyro().
 *
 * In sync mode: Aligns accel and gyro  by last sample,
 * calls onFifoSync() with synchronized 6-axis data.
 *
 */
bool BMI270::processFifo()
{
    uint16_t fifoLength = 0;
    BMI2_INTF_RETURN_TYPE rslt = bmi2_get_fifo_length(&fifoLength, &mBmi2Dev);
    if (rslt != BMI2_OK || fifoLength == 0) {
        return false;
    }

    if (!readFifo(fifoLength)) {
        return false;
    }

    // Taken when the read is over: it went to the end of the FIFO, so the
    // newest frame it brought is no older than the read's last bytes took.
    const uint64_t timestamp = getSystemTime();
    const uint16_t total     = mFifoFrame.length;

    if (!fifoIsTimed()) {
        deliverFrames(0, total, timestamp, nullptr, false);
        return true;
    }

    // An InputConfig frame parts the frames taken before a change of the
    // sensors' settings from those taken after it, and the two may differ in
    // rate: each stretch goes out on its own. The frame names the counter
    // value of the frame after it, so the stretch before it ends just short
    // of that. The last stretch ends where the read emptied the FIFO, at the
    // counter value the part closed it with.
    Bmi270Fifo::FrameReader reader(mFifoBuffer, total);
    Bmi270Fifo::Frame       frame;
    uint16_t                begin          = 0;
    uint16_t                end            = 0;
    uint32_t                readSensorTime = 0;
    bool                    emptied        = false;
    bool                    skipped        = false;

    for (;;) {
        end = reader.offset();

        if (!reader.next(frame)) {
            break;
        }

        if (frame.type == Bmi270Fifo::FrameType::InputConfig) {
            const uint32_t before = (Bmi270Fifo::configSensorTime(frame) - 1u) & Bmi270Fifo::kCounterMask;

            deliverFrames(begin, end, timestamp, &before, skipped);
            applyInputConfig(frame);
            begin   = reader.offset();
            skipped = false;
        } else if (frame.type == Bmi270Fifo::FrameType::SensorTime) {
            readSensorTime = Bmi270Fifo::sensorTime(frame);
            emptied        = true;
            break;
        } else if (frame.type == Bmi270Fifo::FrameType::Skip) {
            // The FIFO filled up and the part dropped its oldest frames.
            skipped = true;
        }
    }

    deliverFrames(begin, end, timestamp, emptied ? &readSensorTime : nullptr, skipped);

    return true;
}

/**
 * @brief Give the listener the frames of a stretch of mFifoBuffer.
 *
 * All the frames of a stream in the stretch were taken at one rate. With
 * the FIFO timed, each gets the value the part's counter had when it was
 * taken: counted back from where the stretch is known to end, or on from
 * the last frame given out when that is not known. The system time of the
 * read goes with every batch as it is; what time that makes each frame's is
 * the listener's to work out.
 *
 * @param begin     Where the stretch starts in mFifoBuffer.
 * @param end       Where it ends.
 * @param timestamp System time of the read, us.
 * @param endTime   A counter value the stretch's frames stand no later
 *                  than, and a running stream's newest stands within a
 *                  period of: the counter when the read emptied the FIFO,
 *                  or the count before the one an InputConfig frame names.
 *                  nullptr when the read stopped short of the FIFO's end.
 * @param skipped   Whether a Skip frame stood before the stretch's frames.
 */
void BMI270::deliverFrames(uint16_t begin, uint16_t end, uint64_t timestamp, const uint32_t* endTime, bool skipped)
{
    if (end <= begin) {
        return;
    }

    const bool     timed    = fifoIsTimed();
    const uint16_t capacity = static_cast<uint16_t>(std::min<uint32_t>((end - begin) / Bmi270Fifo::kReadingSize + 1u,
                                                                       kFifoMaxSamples));

    // The vendor's extraction, over this stretch of the buffer only.
    mFifoFrame.data               = mFifoBuffer + begin;
    mFifoFrame.length             = static_cast<uint16_t>(end - begin);
    mFifoFrame.acc_byte_start_idx = 0;
    mFifoFrame.gyr_byte_start_idx = 0;

    uint16_t accelFrameCount = capacity;
    uint16_t gyroFrameCount  = capacity;

    // Extract accelerometer data
    bmi2_extract_accel(mFifoSensorData, &accelFrameCount, &mFifoFrame, &mBmi2Dev);

    Bmi270Fifo::StreamGrid::Batch accelBatch {};
    accelBatch.count = accelFrameCount;

    if (timed) {
        accelBatch = Bmi270Fifo::placeFrames(mAccelGrid, mAccelOdr,
                                             mSensorEnabled[static_cast<uint8_t>(SensorType::ACCELEROMETER)],
                                             accelFrameCount, endTime, skipped);
        if (accelBatch.dropped != 0) {
            LOG_WARNING("FIFO: %u accel frame(s) with no place on the part's counter\n", accelBatch.dropped);
        }
        accelFrameCount = accelBatch.count;
    }

    // The halves of the pairs are gathered as each stream is placed. Whether
    // the pairs go out is decided when both have been: placing a stream may
    // change the rate it is taken to run at.
    const bool syncMode = isSyncMode();

    if (accelFrameCount > 0) {
        for (uint16_t i = 0; i < accelFrameCount; i++) {
            const bmi2_sens_axes_data& frame = mFifoSensorData[i + accelBatch.dropped];

            mFifoRawDataBuffer[i].sensorTime = (accelBatch.first + i * accelBatch.step) & Bmi270Fifo::kCounterMask;
            mFifoRawDataBuffer[i].x          = frame.x;
            mFifoRawDataBuffer[i].y          = frame.y;
            mFifoRawDataBuffer[i].z          = frame.z;
        }

        if (mpListener) {
            mpListener->onFifoAccel(mFifoRawDataBuffer, accelFrameCount, mAccelScale, timestamp);
        }

        if (syncMode) {
            const uint16_t offset = kFifoMaxSamples - accelFrameCount;

            LOG_DEBUG("SyncMode offset  = %" PRIu16 "\n", offset);

            for (uint16_t i = 0; i < accelFrameCount; i++) {
                mFifoSyncBuffer[i + offset].sensorTime = mFifoRawDataBuffer[i].sensorTime;
                mFifoSyncBuffer[i + offset].accel_x    = mFifoRawDataBuffer[i].x;
                mFifoSyncBuffer[i + offset].accel_y    = mFifoRawDataBuffer[i].y;
                mFifoSyncBuffer[i + offset].accel_z    = mFifoRawDataBuffer[i].z;
            }
        }

    }

    // Extract gyroscope data
    bmi2_extract_gyro(mFifoSensorData, &gyroFrameCount, &mFifoFrame, &mBmi2Dev);

    Bmi270Fifo::StreamGrid::Batch gyroBatch {};
    gyroBatch.count = gyroFrameCount;

    if (timed) {
        gyroBatch = Bmi270Fifo::placeFrames(mGyroGrid, mGyroOdr,
                                            mSensorEnabled[static_cast<uint8_t>(SensorType::GYROSCOPE)],
                                            gyroFrameCount, endTime, skipped);
        if (gyroBatch.dropped != 0) {
            LOG_WARNING("FIFO: %u gyro frame(s) with no place on the part's counter\n", gyroBatch.dropped);
        }
        gyroFrameCount = gyroBatch.count;
    }

    if (gyroFrameCount > 0) {
        for (uint16_t i = 0; i < gyroFrameCount; i++) {
            const bmi2_sens_axes_data& frame = mFifoSensorData[i + gyroBatch.dropped];

            mFifoRawDataBuffer[i].sensorTime = (gyroBatch.first + i * gyroBatch.step) & Bmi270Fifo::kCounterMask;
            mFifoRawDataBuffer[i].x          = frame.x;
            mFifoRawDataBuffer[i].y          = frame.y;
            mFifoRawDataBuffer[i].z          = frame.z;
        }

        if (mpListener) {
            mpListener->onFifoGyro(mFifoRawDataBuffer, gyroFrameCount, mGyroScale, timestamp);
        }

        if (syncMode) {
            const uint16_t offset = kFifoMaxSamples - gyroFrameCount;

            LOG_DEBUG("SyncMode offset  = %" PRIu16 "\n", offset);

            for (uint16_t i = 0; i < gyroFrameCount; i++) {
                mFifoSyncBuffer[i + offset].gyro_x = mFifoRawDataBuffer[i].x;
                mFifoSyncBuffer[i + offset].gyro_y = mFifoRawDataBuffer[i].y;
                mFifoSyncBuffer[i + offset].gyro_z = mFifoRawDataBuffer[i].z;
            }
        }
    }

    // The two streams go out as pairs when these frames of theirs were taken
    // at one rate.
    const bool sync = syncMode && (!timed || mAccelGrid.odr() == mGyroGrid.odr());

    if (sync && accelFrameCount > 0 && gyroFrameCount > 0 && mpListener) {
        const uint16_t syncCount = std::min(accelFrameCount, gyroFrameCount);
        const uint16_t offset    = kFifoMaxSamples - syncCount;
        LOG_DEBUG("SyncMode offset  = %" PRIu16 "\n", offset);
        mpListener->onFifoSync(&mFifoSyncBuffer[offset], syncCount, mAccelScale, mGyroScale, timestamp);
    }

    mFifoFrame.data = mFifoBuffer;

}

/**
 * @brief Take in an InputConfig frame of the FIFO.
 *
 * The part puts one before the first frame taken with new settings, and
 * says in it which settings those are and what its counter is at that
 * frame. Up to it, a stream whose rate was written anew still runs at the
 * old one.
 */
void BMI270::applyInputConfig(const Bmi270Fifo::Frame& frame)
{
    const uint8_t  changes = Bmi270Fifo::configChanges(frame);
    const uint32_t from    = Bmi270Fifo::configSensorTime(frame);

    if (changes & Bmi270Fifo::kAccelConfChanged) {
        mAccelGrid.setOdr(mAccelOdr);
    }

    if (changes & Bmi270Fifo::kGyroConfChanged) {
        mGyroGrid.setOdr(mGyroOdr);
    }

    mAccelGrid.notBefore(from);
    mGyroGrid.notBefore(from);
}

/**
 * @brief A stream's rate was written to the part: tell its frames' grid.
 *
 * A running stream that feeds a timed FIFO keeps its old rate until the part
 * says in the FIFO that the new one has taken effect. Any other takes it
 * at once: nothing will say when. One that does not run has no rate it
 * could go on at for a while, either.
 */
void BMI270::rateWritten(Bmi270Fifo::StreamGrid& grid, uint8_t odr, bool running, bool inFifo)
{
    if (!running || !inFifo || !fifoIsTimed()) {
        grid.setOdr(odr, running);
    }
}

/// Whether the part has a sensor switched on, by its bit in PWR_CTRL.
bool BMI270::isPowered(uint8_t mask)
{
    uint8_t pwrCtrl = 0;

    return (bmi2_get_regs(BMI2_PWR_CTRL_ADDR, &pwrCtrl, 1, &mBmi2Dev) == BMI2_OK) && ((pwrCtrl & mask) != 0);
}

bool BMI270::fifoIsTimed() const
{
    return mFifoEnabled && mFifoConfig.headerEnabled && mFifoConfig.sensorTimeEnabled;
}

/**
 * @brief Read what the FIFO holds into mFifoBuffer, whole frames only.
 *
 * In header mode with the sensortime frame on, the read is taken to the end
 * of the FIFO: the part sends that frame only to a read that empties it. The
 * FIFO fills while it is read, and this thread may be held up between asking
 * for the fill level and reading, so one read may fall short. It is then
 * followed by another, sized from the level asked for anew, until the frame
 * comes. A read that stopped inside a frame gets that frame again from the
 * part, so the bytes of it already here are dropped.
 *
 * @param fifoLength Fill level the part reported.
 * @retval true when mFifoFrame holds something to extract.
 */
bool BMI270::readFifo(uint16_t fifoLength)
{
    const bool toTheEnd = mFifoConfig.headerEnabled && mFifoConfig.sensorTimeEnabled;

    if (!toTheEnd) {
        mFifoFrame.length = fifoLength + mBmi2Dev.dummy_byte;

        return (bmi2_read_fifo_data(&mFifoFrame, &mBmi2Dev) == BMI2_OK);
    }

    uint16_t total = 0;     // Whole frames in the buffer
    uint16_t want  = Bmi270Fifo::readLength(fifoLength, kFifoBufferSize);

    for (uint8_t round = 0; round < kFifoReadRounds; ++round) {
        if (round == 0) {
            // Through the vendor call, which also sets the frame structure
            // up for the extraction to come.
            mFifoFrame.length = want;

            if (bmi2_read_fifo_data(&mFifoFrame, &mBmi2Dev) != BMI2_OK) {
                return false;
            }
        } else {
            const BMI2_INTF_RETURN_TYPE rslt = mBmi2Dev.read(BMI2_FIFO_DATA_ADDR, mFifoBuffer + total, want,
                                                             mBmi2Dev.intf_ptr);

            mBmi2Dev.delay_us((mBmi2Dev.aps_status == BMI2_ENABLE) ? 450 : 2, mBmi2Dev.intf_ptr);

            // What the reads before this one brought is good as it is.
            if (rslt != BMI2_INTF_RET_SUCCESS) {
                break;
            }
        }

        Bmi270Fifo::FrameReader reader(mFifoBuffer, static_cast<uint16_t>(total + want), total);
        Bmi270Fifo::Frame       frame;
        bool                    emptied = false;

        while (reader.next(frame)) {
            if (frame.type == Bmi270Fifo::FrameType::SensorTime) {
                emptied = true;
                break;
            }
        }

        total = reader.offset();

        if (emptied || reader.end() == Bmi270Fifo::ReadEnd::Filler) {
            break;
        }

        if (reader.end() == Bmi270Fifo::ReadEnd::Unknown) {
            LOG_WARNING("FIFO: unknown frame header 0x%02X\n", mFifoBuffer[total]);
            break;
        }

        uint16_t more = 0;
        if (bmi2_get_fifo_length(&more, &mBmi2Dev) != BMI2_OK) {
            break;
        }

        want = Bmi270Fifo::readLength(more, static_cast<uint16_t>(kFifoBufferSize - total));
        if (want < Bmi270Fifo::kSensorTimeFrameSize) {
            break;
        }
    }

    mFifoFrame.length = total;

    return (total > 0);
}

bool BMI270::flushFifo()
{
    uint8_t cmd = BMI2_FIFO_FLUSH_CMD;
    return (bmi2_set_regs(BMI2_CMD_REG_ADDR, &cmd, 1, &mBmi2Dev) == BMI2_OK);
}

uint16_t BMI270::getFifoLength()
{
    uint16_t length = 0;
    bmi2_get_fifo_length(&length, &mBmi2Dev);
    return length;
}

uint16_t BMI270::calculateFifoWatermark(uint32_t periodMs, bool headerEnabled, uint32_t reserveMs) const
{
    const uint8_t accelIndex = static_cast<uint8_t>(SensorType::ACCELEROMETER);
    const uint8_t gyroIndex  = static_cast<uint8_t>(SensorType::GYROSCOPE);

    const uint32_t accelPeriod = mSensorEnabled[accelIndex] ? Bmi270Odr::periodUs(mAccelOdr) : 0;
    const uint32_t gyroPeriod  = mSensorEnabled[gyroIndex] ? Bmi270Odr::periodUs(mGyroOdr) : 0;

    const uint16_t limit = Bmi270Fifo::limitWithReserve(accelPeriod, gyroPeriod, reserveMs, headerEnabled,
                                                        kFifoWatermarkLimit);

    return Bmi270Fifo::watermarkBytes(accelPeriod, gyroPeriod, periodMs, headerEnabled, limit);
}

bool BMI270::setFifoWatermark(uint16_t bytes)
{
    if (bmi2_set_fifo_wm(bytes, &mBmi2Dev) != BMI2_OK) {
        return false;
    }

    mFifoConfig.watermarkBytes = bytes;
    return true;
}

} // namespace Hardware
