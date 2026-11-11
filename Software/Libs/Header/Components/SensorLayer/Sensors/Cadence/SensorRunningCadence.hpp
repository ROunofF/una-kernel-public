/**
 ******************************************************************************
 * @file    SensorRunningCadence.hpp
 * @brief   Periodic RUNNING_CADENCE sensor (cadence only).
 *
 * Cadence is derived from the cumulative STEP_COUNTER stream (windowed step
 * rate), not from per-step detector events - the counter tracks true cadence
 * while individual inter-step timestamps are ms-quantised/batched.
 *
 * Implied step length is derived SDK-side at FIT record-write time, not here.
 ******************************************************************************
 */

#ifndef SENSOR_RUNNING_CADENCE_HPP
#define SENSOR_RUNNING_CADENCE_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Sensors/Cadence/CadenceEstimator.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SensorLayer/Core/SourceTick.hpp"

#include <atomic>

namespace Sensor
{

class RunningCadence : public Interface::ISensor,
                       public Sensor::ISensorDriverCtrl,
                       public SDK::Interface::ISensorDataListener
{
public:
    RunningCadence(Sensor::Driver& stepCounterDriver,
                   Sensor::Driver& stepDetectorDriver);
    ~RunningCadence() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
    float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

private:
    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* data,
                      uint16_t                 count,
                      uint16_t                 stride) override;

    void publishSample(uint32_t nowMs);

    static constexpr float kMinPeriodMs = 1000.0f;

    Sensor::Driver     mDriver;
    Sensor::Connection mStepCounterConnection;
    // Held only to keep the BMI270 per-step interrupt alive so the step-counter
    // register is read every step (its own watermark interrupts only every 20
    // steps, ~7 s at 170 SPM, which would exceed kMaxStepGapUs). This makes
    // RunningCadence self-sufficient and independent of whether the Activity
    // FSM happens to enable the detector elsewhere. Its samples are ignored.
    Sensor::Connection mStepDetectorConnection;
    CadenceEstimator   mEstimator;
    Sensor::SourceTick mTick;
    std::atomic<bool>  mActive{false};
};

} // namespace Sensor

#endif // SENSOR_RUNNING_CADENCE_HPP
