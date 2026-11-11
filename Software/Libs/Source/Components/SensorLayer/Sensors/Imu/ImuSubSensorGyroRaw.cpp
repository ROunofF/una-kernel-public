/**
 ******************************************************************************
 * @file    ImuSubSensorGyroRaw.hpp
 * @brief   SubSensor for the raw gyroscope
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.GyroRaw"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorGyroRaw.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp"
#include "ImuFifoCadence.hpp"
#include "System.hpp"

namespace Sensor
{
ImuSubSensorGyroRaw::ImuSubSensorGyroRaw(Interface::IImu&           imu,
                                         Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::GYROSCOPE_RAW,
              SDK::SensorDataParser::GyroscopeRaw::getFieldsNumber(),
              ctrl)
    , mTimer()
    , mImu(imu)
    , mQueue()
    , mLastTimestamp(0)
    , mLastTimestampValid(false)
{
    mQueue.init(LOG_MODULE_PRX);

    // For the processed channel, which is handed readings a batch at a time.
    mDriver.setSourceStepTrack(&mSteps);
}

Sensor::Driver& ImuSubSensorGyroRaw::getDriver()
{
    return mDriver;
}

bool ImuSubSensorGyroRaw::isActive()
{
    return mTimer.getActive();
}

float ImuSubSensorGyroRaw::start(float period)
{
    LOG_DEBUG("start\n");

    if (!(period >= mMinPeriod)) {
        period = mMinPeriod;
    }

    LOG_DEBUG("period=%.3f\n", period);

    // Asked for as a period: the rates of the part are not whole numbers of
    // Hz, and the part picks one that is no slower than this. Rounded to the
    // microsecond, not cut: a period that float arithmetic leaves a hair under
    // one of the part's own would otherwise ask for the rate above it.
    mImu.enableGyro(static_cast<uint32_t>(period * 1000.0f + 0.5f));

    mTimer.start(period);

    return period;
}

void ImuSubSensorGyroRaw::stop()
{
    LOG_DEBUG("stop\n");

    mTimer.stop();

    mImu.disableGyro();
}

float ImuSubSensorGyroRaw::updatePeriod(float period)
{
    if (!(period >= mMinPeriod)) {
        period = mMinPeriod;
    }

    LOG_DEBUG("period=%.3f\n", period);

    // Asked for as a period: the rates of the part are not whole numbers of
    // Hz, and the part picks one that is no slower than this. Rounded to the
    // microsecond, not cut: a period that float arithmetic leaves a hair under
    // one of the part's own would otherwise ask for the rate above it.
    mImu.enableGyro(static_cast<uint32_t>(period * 1000.0f + 0.5f));

    mTimer.start(period);

    return period;
}

void ImuSubSensorGyroRaw::updateLatency(uint32_t latency)
{
    mLatency.store(latency, std::memory_order_relaxed);
    mImu.setFifoLatency(Interface::IImu::FifoChannel::GYRO, latency);
}

float ImuSubSensorGyroRaw::getMinPeriod()
{
    return mMinPeriod;
}

float ImuSubSensorGyroRaw::getCurrentPeriod()
{
    return static_cast<float>(mTimer.getInterval());
}

const char* ImuSubSensorGyroRaw::getDescription()
{
    return "BMI270 gyroscope raw";
}

void ImuSubSensorGyroRaw::sensorRefresh()
{
    // Readings are handed on as soon as they are here. The source delivers
    // them in batches of its own; waiting for a timer on top of that would
    // only hold each batch back.
    const size_t count = mQueue.size();

    if (count != 0) {
        LOG_DEBUG("ENTRY\n");

        Interface::IImu::SensorRawData data{};
        auto& sample = mDriver.getDataSample();
        // Never waits: the source drops its oldest reading when the queue
        // is full, so fewer may be left than were counted, and a wait here
        // would hold the whole sweep.
        for (size_t idx = 0; idx < count && mQueue.pop(data, 0); ++idx) {
            // Counted against the step it was taken at.
            mDriver.setSourceStepUs(mSteps.stepAt(data.timestamp));
            sample.setTimestampUs(data.timestamp);
            sample.i[SDK::SensorDataParser::GyroscopeRaw::Field::X] = data.x;
            sample.i[SDK::SensorDataParser::GyroscopeRaw::Field::Y] = data.y;
            sample.i[SDK::SensorDataParser::GyroscopeRaw::Field::Z] = data.z;
            mDriver.pushDataSample();
        }

        // The FIFO is read within what was asked for this stream, held to the
        // bounds of the IMU; the other stream can only make that sooner.
        mDriver.endOfBatch(ImuFifoCadence::periodMs(true, mLatency.load(std::memory_order_relaxed), false, 0));

        LOG_DEBUG("EXIT\n");
    }
}

void ImuSubSensorGyroRaw::addNewData(const Interface::IImu::SensorRawData* data, size_t count, uint32_t periodUs)
{
    if (isActive()) {
        // Noted by the stamp it holds from, not reported here: frames still
        // queued from before a rate change are published under the step
        // they were taken at.
        if (count != 0) {
            mSteps.note(data[0].timestamp, periodUs);
        }

#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
        if (!mLastTimestampValid) {
            mLastTimestampValid = true;
        } else {
            const uint32_t ts = data[0].timestamp / 1000;
            int delta = mLastTimestamp >= ts ?
                        -(ts - mLastTimestamp) :
                        mLastTimestamp - ts;

            LOG_DEBUG("%u - %u DELTA = %d\n", mLastTimestamp, ts, delta);
        }
#endif

        mLastTimestamp = data[count - 1].timestamp / 1000;

        for (size_t idx = 0; idx < count; ++idx) {
            if (mQueue.available() == 0) {
                // The oldest makes room. Never waits: the reader may have
                // emptied the queue since it was found full.
                Interface::IImu::SensorRawData tmp {};
                mQueue.pop(tmp, 0);
            }
            if (!mQueue.push(data[idx], 0)) {
                //assert(false);
            }
        }

        // The FIFO has just been read: hand the readings on now rather than
        // at the next refresh tick, which knows nothing of the FIFO's pace.
        if (count != 0) {
            Sensor::Manager::getInstance().wake();
        }
    }
}

} // namespace Sensor
