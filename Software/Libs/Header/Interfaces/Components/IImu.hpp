/**
 ******************************************************************************
 * @file    IImu.hpp
 * @brief   Inertial measurement unit interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_IIMU_HPP
#define INTERFACE_IIMU_HPP

#include <cstddef>
#include <cstdint>

namespace Interface
{

/**
 * @brief   Inertial measurement unit: an accelerometer and a gyroscope that
 *          share a FIFO, and the features the part works out of them itself.
 *
 * The two streams are handed over in batches, raw, each reading with the
 * system time it was taken at. The features - steps, activity, motion, wrist
 * wake-up - are events.
 *
 * Within a stream a reading's time is always later than the one before it.
 * A time is not later than the read that brought the reading, except where
 * the two cannot both hold: readings that would stand at one instant, as can
 * happen at the first read of a stream, are set a microsecond apart, and the
 * last of them may then pass the read's time by as many microseconds.
 *
 * Enabling, disabling and the FIFO latency are requests: they are queued
 * and acted on by the implementation's own thread, and 'true' means the
 * request was taken. Everything is delivered on that thread.
 */
class IImu
{

public:

    enum class ActivityType : uint8_t {
        STILL   = 0,
        WALKING = 1,
        RUNNING = 2,
        UNKNOWN = 3
    };

    /// A raw reading of the accelerometer or the gyroscope.
    struct SensorRawData {
        int16_t  x = 0;
        int16_t  y = 0;
        int16_t  z = 0;
        uint64_t timestamp = 0;  ///< System time the reading was taken at, us
    };

    /// A raw reading of both, taken together.
    struct SyncSampleData {
        int16_t  accel_x = 0;
        int16_t  accel_y = 0;
        int16_t  accel_z = 0;
        int16_t  gyro_x = 0;
        int16_t  gyro_y = 0;
        int16_t  gyro_z = 0;
        uint64_t timestamp = 0;  ///< System time the pair was taken at, us
    };

    /// The streams that share the FIFO.
    enum class FifoChannel : uint8_t { ACCEL, GYRO };

    /**
     * @brief Sensor events callback interface
     */
    class SensorCallback {
    public:
        // Accelerometer. periodUs is the step the part ran at for this batch.
        virtual void onAccelRaw(const SensorRawData* data, size_t count, float scale, uint32_t periodUs) {}

        // Gyroscope, periodUs as for the accelerometer
        virtual void onGyroRaw(const SensorRawData* data, size_t count, float scale, uint32_t periodUs) {}

        // Synchronous sample Accelerometer + Gyroscope. Both run at one rate
        // in sync mode; periodUs is that rate's step, as for the others.
        virtual void onSyncRaw(const SyncSampleData* data, size_t count, float accelScale, float gyroScale,
                               uint32_t periodUs) { }

        // Steps
        virtual void onStepCounter(uint32_t steps, uint64_t timestamp) {}
        virtual void onStepDetector(uint64_t timestamp) {}

        // Activity
        virtual void onActivity(ActivityType type, uint64_t timestamp) {}

        // Wrist wake-up
        virtual void onWristWakeup(uint64_t timestamp) {}

        // Motion
        virtual void onNoMotion(uint64_t timestamp) {}
        virtual void onMotion(uint64_t timestamp) {}
        virtual void onSigMotion(uint64_t timestamp) {}

    protected:
        virtual ~SensorCallback() = default;
    };

    virtual void setSensorCallback(SensorCallback *pCallback) = 0;

    virtual bool init() = 0;
    virtual bool deinit() = 0;
    virtual bool isInitialized() const = 0;

    virtual bool resetStepCounter() = 0;

    /**
     * @brief How late a reading of a stream may be handed over.
     *
     * The FIFO is read when it has gathered that much, so this sets how often
     * the IMU wakes the system. The two streams share the FIFO and the
     * tighter request paces both. A request for a stream that is off waits
     * until the stream is enabled.
     *
     * @param channel Stream the request is for.
     * @param ms      Latency in ms, 0 to withdraw the request.
     * @return Whether the request was queued.
     */
    virtual bool setFifoLatency(FifoChannel channel, uint32_t ms) = 0;

    // The rate is asked for as a data period in us. The part runs at the
    // slowest of its rates that is no slower than that.
    // Range: 2,4,8,16 g
    virtual bool enableAccel(uint32_t periodUs) = 0;
    virtual bool disableAccel() = 0;
    virtual uint16_t getAccelRange() const = 0;
    virtual void setAccelRange(uint16_t range) = 0;
    virtual float getAccelScale() const = 0;

    // Range: 125,250,500,1000,2000 dps
    virtual bool enableGyro(uint32_t periodUs) = 0;
    virtual bool disableGyro() = 0;
    virtual uint16_t getGyroRange() const = 0;
    virtual void setGyroRange(uint16_t range) = 0;
    virtual float getGyroScale() const = 0;

    virtual bool enableStepCounter() = 0;
    virtual bool disableStepCounter() = 0;

    virtual bool enableStepDetector() = 0;
    virtual bool disableStepDetector() = 0;

    virtual bool enableActivity() = 0;
    virtual bool disableActivity() = 0;

    virtual bool enableWristWakeup() = 0;
    virtual bool disableWristWakeup() = 0;

    virtual bool enableAnyMotion(bool oneShot = true) = 0;
    virtual bool disableAnyMotion() = 0;

    virtual bool enableNoMotion(bool oneShot = true) = 0;
    virtual bool disableNoMotion() = 0;

    virtual bool enableSigMotion(bool oneShot = true) = 0;
    virtual bool disableSigMotion() = 0;

    virtual bool disableAll() = 0;

protected:

    virtual ~IImu() = default;

};

} // namespace Interface

#endif // INTERFACE_IIMU_HPP
