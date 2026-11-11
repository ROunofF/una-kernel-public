/**
 ******************************************************************************
 * @file    SensorGrade.hpp
 * @brief   Derived GRADE sensor: barometric terrain grade at 1 Hz.
 *
 * Subscribes to PRESSURE (MS5837) and GPS_DISTANCE, maintains a 10-second
 * rolling window via GradeEstimator (QNH-independent relative elevation over
 * GPS distance) and publishes GRADE_PCT / GRADE_VALID.
 ******************************************************************************
 */

#ifndef SENSOR_GRADE_HPP
#define SENSOR_GRADE_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Sensors/Grade/GradeEstimator.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "OS.hpp"
#include "SensorLayer/Core/SourceTick.hpp"

#include <atomic>
#include <cstdint>

namespace Sensor
{

class Grade : public Interface::ISensor,
              public Sensor::ISensorDriverCtrl,
              public SDK::Interface::ISensorDataListener
{
public:
    Grade();
    ~Grade() override = default;

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

    // ISensorDataListener
    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override;

private:
    void publishSample(uint32_t nowMs, const GradeEstimator::Sample &sample);

    /// Publish / window-step period (1 Hz, matching GradeEstimator's window).
    static constexpr float    kMinPeriodMs = 1000.0f;
    /// Upper clamp before the uint32_t cast in sdcStart()/sdcUpdatePeriod():
    /// casting a finite float >= 2^32 to uint32_t is undefined behaviour in
    /// C++17. 4294967040 (= 2^32 - 256) is the largest float that still fits in
    /// uint32_t. Real sensor periods are ~kMinPeriodMs; this only guards against
    /// a pathological input and never clamps a realistic value.
    static constexpr float    kMaxPeriodMs = 4294967040.0f;
    /// Input subscription period for the source sensors, ms.
    static constexpr uint16_t kInputPeriodMs = 1000;
    /// Fixed reference pressure for relative altitude; cancels in the 10-s
    /// delta, so QNH calibration is never required.
    static constexpr float    kReferencePressurePa = 101325.0f;
    /// How old a baro sample a tick still takes, ms: the one of its own
    /// second or the one before it, and none older.
    static constexpr uint32_t kAltitudeMaxAgeMs = (kInputPeriodMs * 5U) / 2U;

    Sensor::Connection  mPressureConnection;
    Sensor::Connection  mDistanceConnection;
    Sensor::Driver      mDriver;
    GradeEstimator      mEstimator;
    Sensor::SourceTick  mTick;

    OS::Mutex mDataMutex;
    float     mLatchedAltitudeM = 0.0f;
    uint32_t  mAltitudeAtMs     = 0;      ///< When the latched altitude was measured.
    float     mLatchedDistanceM = 0.0f;
    bool      mHaveAltitude     = false;  ///< The latest baro sample of this stream is valid.
    bool      mPressureGlitch   = false;  ///< An invalid/NaN baro sample arrived this tick.
    bool      mGpsDistanceValid = false;

    std::atomic<bool> mActive{false};
}; // class Grade

} // namespace Sensor

#endif // SENSOR_GRADE_HPP
