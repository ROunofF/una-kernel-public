/**
 ******************************************************************************
 * @file    ImuSubSensorStepCounter.hpp
 * @brief   Step-counter sub-sensor for BMI270
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_STEP_COUNTER_HPP
#define IMU_SUB_SENSOR_STEP_COUNTER_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "IImu.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class ImuSubSensorStepCounter : public Sensor::ISubSensor,
                                public Interface::ISensor
{
public:
    ImuSubSensorStepCounter(Interface::IImu&           imu,
                            Sensor::ISensorDriverCtrl& ctrl);
    ~ImuSubSensorStepCounter() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void updateStepCounter(uint32_t raw);

private:
    // The BMI270 on-chip step counter is a free-running monotonic counter:
    // it is NOT zeroed when the STEP_COUNTER feature is disabled - only a
    // full chip soft-reset (lazy deinit / power-up) resets it. It is tracked
    // as a monotonic source split into "epochs", each running from the chip
    // value at its start (mEpochBase) until the next soft-reset. getData()
    // then stays monotonic across both feature disable/enable cycles and
    // chip soft-resets.
    uint32_t getData() const;
    void     publishData();

    Sensor::Driver mDriver;
    Interface::IImu& mImu;

    uint32_t mOffset    = 0;   // banked steps from completed epochs
    uint32_t mEpochBase = 0;   // chip value at the start of the current epoch
    uint32_t mLastRaw   = 0;   // last accepted chip step-counter reading
    bool     mPrimed    = false;

    // Single-sample debounce: a lone reading below mLastRaw (I2C glitch) is
    // held pending and dropped if the next reading returns to the old
    // regime; a genuine soft-reset is committed once a second reading
    // corroborates it.
    bool     mResetPending = false;
    uint32_t mResetCandRaw = 0;

    bool      mActive    = false;
    bool      mDirty     = false;
    uint32_t  mTimestamp = 0;
    OS::Mutex mDataMutex;
};

} // namespace Sensor

#endif // IMU_SUB_SENSOR_STEP_COUNTER_HPP
