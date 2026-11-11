/**
 ******************************************************************************
 * @file    ImuSubSensorMotionDetect.cpp
 * @brief   Motion-detect sub-sensor for BMI270
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.Motion"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorMotionDetect.hpp"
#include "SensorLayer/Core/SensorManager.hpp"

namespace Sensor
{

ImuSubSensorMotionDetect::ImuSubSensorMotionDetect(Interface::IImu&           imu,
                                                   Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::MOTION_DETECT,
              SDK::SensorDataParser::MotionDetect::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
    , mImu(imu)
    , mMotion(SDK::SensorDataParser::MotionDetect::Motion::UNKNOWN)
    , mState(State::IDLE)
{
    mQueue.init(LOG_MODULE_PRX);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorMotionDetect::getDriver()
{
    return mDriver;
}

bool ImuSubSensorMotionDetect::isActive()
{
    //// Lock-free read: a single enum is atomic on every supported target.
    //// Worst case a transient mismatch in motionDetected() yields one extra
    //// stale entry in mQueue, which handleMotionEvent() will ignore.
    return mState != State::IDLE;
}

float ImuSubSensorMotionDetect::start(float period)
{
    LOG_INFO("start\n");

    OS::MutexCS cs(mDataMutex);

    mQueue.clear();
    mMotion = SDK::SensorDataParser::MotionDetect::Motion::NO_MOTION;
    armMotion();                  //// Sets mState to WAITING_FOR_MOTION

    //// publishData() is intentionally NOT called here. Sensor::Driver::connect()
    //// holds its mutex while invoking start() and would deadlock on
    //// publishData() (which re-locks the driver mutex). The initial sample is
    //// instead emitted from newConnection(), which connect() calls after
    //// releasing its mutex.

    return period;
}

void ImuSubSensorMotionDetect::stop()
{
    LOG_INFO("stop\n");

    OS::MutexCS cs(mDataMutex);

    disarmAll();

    mQueue.clear();
}

const char* ImuSubSensorMotionDetect::getDescription()
{
    return "BMI270 motion detection";
}

void ImuSubSensorMotionDetect::newConnection()
{
    OS::MutexCS cs(mDataMutex);

    publishData(mMotion, OS::GetTimeMs());
}

/**
 * @brief Drain accumulated motion events from BMI270.
 *
 * @details
 * Invoked by Sensor::Manager at its tick rate (the minimum getRefreshPeriod()
 * across all active sensors). The method is purely event-driven -- there is no
 * internal timer or rate gate. Every event currently pending in mQueue is
 * processed in order; if the queue is empty the call is a no-op.
 */
void ImuSubSensorMotionDetect::sensorRefresh()
{
    QueuedMotion event;
    while (mQueue.pop(event, 0)) {   // non-blocking pop
        handleMotionEvent(event.motion, event.timestamp);
    }
}

/**
 * @brief Marshal a chip event from the ImuBosch thread into mQueue.
 *
 * @details
 * Called from ImuBosch's callback context. The check on mState is a fast
 * lock-free guard against pushing events while the sub-sensor is stopped --
 * stale events that slip through the race are ignored later in
 * handleMotionEvent().
 */
void ImuSubSensorMotionDetect::motionDetected(Motion motion)
{
    if (mState == State::IDLE) {
        return;
    }
    LOG_DEBUG("rx motion=%u\n", static_cast<uint32_t>(motion));
    mQueue.push({ motion, OS::GetTimeMs() }, 1);
    Sensor::Manager::getInstance().wake();
}

void ImuSubSensorMotionDetect::publishData(SDK::SensorDataParser::MotionDetect::Motion motion,
                                           uint32_t                                    timestamp)
{
    LOG_DEBUG("tx motion=%u\n", static_cast<uint32_t>(motion));
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(timestamp);
    sample.u[SDK::SensorDataParser::MotionDetect::Field::ID] = static_cast<uint32_t>(motion);
    mDriver.pushDataSample();
}

/**
 * @brief Apply one motion event to the sub-sensor state.
 *
 * @details
 * Two-state FSM modeled after Android's sensor framework:
 *
 *  WAITING_FOR_MOTION (AnyMotion armed):
 *    - Motion::ANY -> publish MOTION (edge), arm IN_MOTION.
 *    - Motion::SIG / Motion::NO  -> not expected (features disabled), ignored.
 *
 *  IN_MOTION (SigMotion + NoMotion armed in parallel):
 *    - Motion::SIG -> publish SIG_MOTION, stay in IN_MOTION.
 *                    SigMotion is left armed on the chip so it can fire
 *                    again every block_size window (~5 s @ 50 Hz). This is
 *                    intentional heartbeat behavior -- consumers see a fresh
 *                    SIG_MOTION sample for every confirmed significant-motion
 *                    block, including subsequent bursts during a long session.
 *    - Motion::NO  -> publish NO_MOTION (edge), arm WAITING_FOR_MOTION.
 *    - Motion::ANY -> not expected (AnyMotion disabled), ignored.
 *
 *  IDLE:
 *    - All events ignored (sub-sensor stopped).
 *
 * Holds mDataMutex across the whole operation to keep (mMotion, mState) and
 * the published sample consistent. Lock order matches newConnection().
 */
void ImuSubSensorMotionDetect::handleMotionEvent(Motion motion, uint32_t timestamp)
{
    OS::MutexCS cs(mDataMutex);

    switch (mState) {
    case State::WAITING_FOR_MOTION:
        if (motion == Motion::ANY) {
            mMotion = SDK::SensorDataParser::MotionDetect::Motion::MOTION;
            publishData(mMotion, timestamp);
            armInMotion();
        }
        break;

    case State::IN_MOTION:
        if (motion == Motion::SIG) {
            //// Heartbeat re-publish. SigMotion stays armed on the chip --
            //// no chip-side action, just forward the signal.
            mMotion = SDK::SensorDataParser::MotionDetect::Motion::SIG_MOTION;
            publishData(mMotion, timestamp);
        } else if (motion == Motion::NO) {
            mMotion = SDK::SensorDataParser::MotionDetect::Motion::NO_MOTION;
            publishData(mMotion, timestamp);
            armMotion();
        }
        break;

    case State::IDLE:
    default:
        break;
    }
}

/**
 * @brief Arm BMI270 to detect onset of motion (transition to IN_MOTION).
 *
 * @details
 * Target chip state: only AnyMotion armed. SigMotion and NoMotion silenced.
 *
 * Uses the "enable new feature before releasing the previous one" pattern:
 * AnyMotion is enabled first (refcount goes up) and only then SigMotion /
 * NoMotion are released. BMI270's accel refcount therefore never drops to
 * zero across the transition, so the chip stays initialized and we avoid a
 * full firmware reupload (~150 ms via I2C).
 *
 * oneShot=false: the chip-level lifecycle is owned by the FSM, not by the
 * ImuBosch one-shot auto-release path. Spam events generated by AnyMotion
 * before this method runs are tolerated and ignored by handleMotionEvent.
 */
void ImuSubSensorMotionDetect::armMotion()
{
    mImu.enableAnyMotion(false);
    mImu.disableSigMotion();
    mImu.disableNoMotion();
    mState = State::WAITING_FOR_MOTION;
    LOG_DEBUG("State: WAITING_FOR_MOTION\n");
}

/**
 * @brief Arm BMI270 to track ongoing motion (transition to IN_MOTION).
 *
 * @details
 * Target chip state: SigMotion and NoMotion armed in parallel. AnyMotion
 * silenced (no longer interesting -- motion has already started).
 *
 * SigMotion evaluates statistical blocks of `block_size` samples (~5 s @
 * 50 Hz). If motion remains significant, it will keep firing roughly once
 * per block, and we forward each event as a SIG_MOTION heartbeat.
 *
 * NoMotion fires once after `noMotionConfig.duration` of stillness (~3 s
 * with default config) and triggers the transition back to
 * WAITING_FOR_MOTION.
 *
 * Same enable-before-disable ordering as armMotion() for the same reason:
 * keep refcount > 0 across the transition.
 */
void ImuSubSensorMotionDetect::armInMotion()
{
    mImu.enableSigMotion(false);
    mImu.enableNoMotion(false);
    mImu.disableAnyMotion();
    mState = State::IN_MOTION;
    LOG_DEBUG("State: IN_MOTION\n");
}

/**
 * @brief Disable every motion-related feature on BMI270.
 *
 * @details
 * Used by stop(). Safe to call regardless of current chip state because
 * ImuBosch ignores releases for features that are not currently registered.
 * On stop the refcount drops to zero and BMI270 deinitializes the chip,
 * which is the desired cleanup behavior.
 */
void ImuSubSensorMotionDetect::disarmAll()
{
    mImu.disableAnyMotion();
    mImu.disableSigMotion();
    mImu.disableNoMotion();
    mState = State::IDLE;
    LOG_DEBUG("State: IDLE\n");
}

} // namespace Sensor
