/**
 ******************************************************************************
 * @file    ImuSubSensorMotionDetect.hpp
 * @brief   Motion-detect sub-sensor for BMI270
 ******************************************************************************
 */

#ifndef IMU_SUB_SENSOR_MOTION_DETECT_HPP
#define IMU_SUB_SENSOR_MOTION_DETECT_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "IImu.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

/**
 * @brief Motion-detect sub-sensor for BMI270.
 *
 * @details
 * Event-based sub-sensor that publishes one of three motion states
 * (NO_MOTION / MOTION / SIG_MOTION) using BMI270's hardware AnyMotion,
 * SigMotion and NoMotion features.
 *
 * Two-state machine modeled after Android's sensor framework:
 *
 *   WAITING_FOR_MOTION
 *      Chip-side: AnyMotion armed (only).
 *      Publishes one MOTION on transition out (edge semantics, same as
 *      Android TYPE_MOTION_DETECT).
 *
 *   IN_MOTION
 *      Chip-side: SigMotion + NoMotion armed in parallel.
 *      Publishes SIG_MOTION every time the chip fires it (heartbeat --
 *      we do not have an Android-style re-register API, so we forward
 *      every significant-motion confirmation, ~one per BMI270 block_size
 *      window). Publishes one NO_MOTION on transition out (edge semantics,
 *      same as Android TYPE_STATIONARY_DETECT).
 *
 * Features are armed without the BMI270 one-shot mode (oneShot=false)
 * -- lifecycle is controlled explicitly by armMotion() / armInMotion()
 * with the "enable new feature before releasing the previous one" pattern,
 * so the chip's accel refcount never drops to zero between transitions and
 * the chip never reinitializes mid-cycle.
 *
 * Chip events arrive on the ImuBosch thread; motionDetected() marshals them
 * into mQueue. Sensor::Manager drains the queue in sensorRefresh() and runs
 * the state machine on its own thread under mDataMutex.
 */
class ImuSubSensorMotionDetect : public Sensor::ISubSensor,
                                 public Interface::ISensor
{
public:
    enum class Motion {
        NO,
        ANY,
        SIG
    };

    ImuSubSensorMotionDetect(Interface::IImu&           imu,
                             Sensor::ISensorDriverCtrl& ctrl);
    ~ImuSubSensorMotionDetect() override = default;

    // ISubSensor
    Sensor::Driver& getDriver()                override;
    bool            isActive()                 override;
    float           start(float period)        override;
    void            stop()                     override;
    const char*     getDescription()           override;
    void            newConnection()            override;

    // ISensor
    void sensorRefresh() override;

    void motionDetected(Motion motion);

private:
    enum class State {
        IDLE,                  //// Sub-sensor stopped, nothing armed
        WAITING_FOR_MOTION,    //// AnyMotion armed; waiting for motion to start
        IN_MOTION              //// SigMotion + NoMotion armed; motion ongoing
    };

    struct QueuedMotion {
        Motion   motion;
        uint32_t timestamp;    //// ms, captured when the chip event arrived
    };

    void publishData(SDK::SensorDataParser::MotionDetect::Motion id, uint32_t timestamp);
    void handleMotionEvent(Motion motion, uint32_t timestamp);
    void armMotion();      //// -> WAITING_FOR_MOTION
    void armInMotion();    //// -> IN_MOTION
    void disarmAll();      //// -> IDLE (used by stop())

    Sensor::Driver mDriver;
    Interface::IImu& mImu;

    SDK::SensorDataParser::MotionDetect::Motion mMotion;
    State                                       mState;

    OS::Queue<QueuedMotion, 5> mQueue;
    OS::Mutex                  mDataMutex;
};

} // namespace Sensor

#endif // IMU_SUB_SENSOR_MOTION_DETECT_HPP
