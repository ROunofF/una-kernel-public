/**
 ******************************************************************************
 * @file    ImuSubSensorFusionRaw.hpp
 * @brief   SubSensor for the raw accelerometer and gyroscope pairs
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.FusionRaw"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorFusionRaw.hpp"
#include "ImuFifoCadence.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "System.hpp"

#include <algorithm>

namespace Sensor
{
ImuSubSensorFusionRaw::ImuSubSensorFusionRaw(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::FUSION_RAW,
              SDK::SensorDataParser::FusionRaw::getFieldsNumber(),
              ctrl)
    , mTimer()
    , mDriverXL(SDK::Sensor::Type::ACCELEROMETER_RAW, this)
    , mDriverGY(SDK::Sensor::Type::GYROSCOPE_RAW, this)
    , mQueue()
    , mPeriod(0.0f)
    , mLatency(kDefaultLatency)
{
    mQueue.init(LOG_MODULE_PRX);

    // For the processed channel, which is handed readings a batch at a time.
    mDriver.setSourceStepTrack(&mSteps);
}

Sensor::Driver& ImuSubSensorFusionRaw::getDriver()
{
    return mDriver;
}

bool ImuSubSensorFusionRaw::isActive()
{
    return mTimer.getActive();
}

float ImuSubSensorFusionRaw::start(float period)
{
    LOG_DEBUG("start\n");

    if (period < mMinPeriod) {
        period = mMinPeriod;
    }

    LOG_DEBUG("period=%.3f\n", period);

    mPeriod = period;

    mDriverXL.connect(mPeriod, mLatency.load(std::memory_order_relaxed));
    mDriverGY.connect(mPeriod, mLatency.load(std::memory_order_relaxed));

    mTimer.start(period);

    return period;
}

void ImuSubSensorFusionRaw::stop()
{
    LOG_DEBUG("stop\n");

    mTimer.stop();

    mDriverXL.disconnect();
    mDriverGY.disconnect();
}

float ImuSubSensorFusionRaw::updatePeriod(float period)
{
    if (period < mMinPeriod) {
        period = mMinPeriod;
    }

    LOG_DEBUG("period=%.3f\n", period);

    mPeriod = period;

    mDriverXL.connect(mPeriod, mLatency.load(std::memory_order_relaxed));
    mDriverGY.connect(mPeriod, mLatency.load(std::memory_order_relaxed));

    mTimer.start(period);

    return period;
}

void ImuSubSensorFusionRaw::updateLatency(uint32_t latency)
{
    mLatency.store((latency != 0) ? latency : kDefaultLatency, std::memory_order_relaxed);

    if (isActive()) {
        mDriverXL.connect(mPeriod, mLatency.load(std::memory_order_relaxed));
        mDriverGY.connect(mPeriod, mLatency.load(std::memory_order_relaxed));
    }
}

float ImuSubSensorFusionRaw::getMinPeriod()
{
    return mMinPeriod;
}

float ImuSubSensorFusionRaw::getCurrentPeriod()
{
    return static_cast<float>(mTimer.getInterval());
}

const char* ImuSubSensorFusionRaw::getDescription()
{
    return "BMI270 accelerometer raw";
}

void ImuSubSensorFusionRaw::sensorRefresh()
{
    // Readings are handed on as soon as they are here. The source delivers
    // them in batches of its own; waiting for a timer on top of that would
    // only hold each batch back.
    const size_t count = mQueue.size();

    if (count != 0) {
        LOG_DEBUG("ENTRY\n");
        Interface::IImu::SyncSampleData data{};
        auto& sample = mDriver.getDataSample();
        // Never waits: the source drops its oldest reading when the queue
        // is full, so fewer may be left than were counted, and a wait here
        // would hold the whole sweep.
        for (size_t idx = 0; idx < count && mQueue.pop(data, 0); ++idx) {
            // Counted against the step it was taken at.
            mDriver.setSourceStepUs(mSteps.stepAt(data.timestamp));
            sample.setTimestampUs(data.timestamp);
            sample.i[SDK::SensorDataParser::FusionRaw::Field::ACCEL_X] = data.accel_x;
            sample.i[SDK::SensorDataParser::FusionRaw::Field::ACCEL_Y] = data.accel_y;
            sample.i[SDK::SensorDataParser::FusionRaw::Field::ACCEL_Z] = data.accel_z;
            sample.i[SDK::SensorDataParser::FusionRaw::Field::GYRO_X]  = data.gyro_x;
            sample.i[SDK::SensorDataParser::FusionRaw::Field::GYRO_Y]  = data.gyro_y;
            sample.i[SDK::SensorDataParser::FusionRaw::Field::GYRO_Z]  = data.gyro_z;
            mDriver.pushDataSample();
        }

        // The raw stream hands over within the latency asked of it, and no
        // more often than the FIFO behind it is read, however little is asked.
        mDriver.endOfBatch(std::max(mLatency.load(std::memory_order_relaxed),
                                    ImuFifoCadence::kMinFifoLatencyMs));

        LOG_DEBUG("EXIT\n");
    }
}

void ImuSubSensorFusionRaw::addNewData(const Interface::IImu::SyncSampleData* data, size_t count, uint32_t periodUs)
{
    if (isActive()) {
        // Noted by the stamp it holds from, not reported here: frames still
        // queued from before a rate change are published under the step
        // they were taken at.
        if (count != 0) {
            mSteps.note(data[0].timestamp, periodUs);
        }

        for (size_t idx = 0; idx < count; ++idx) {
            if (mQueue.available() == 0) {
                // The oldest makes room. Never waits: the reader may have
                // emptied the queue since it was found full.
                Interface::IImu::SyncSampleData tmp {};
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

void ImuSubSensorFusionRaw::onSdlNewData(uint16_t                 handle,
                                         const SDK::Sensor::Data* base,
                                         uint16_t                 count,
                                         uint16_t                 stride)
{
    (void) handle;
    (void) base;
    (void) count;
    (void) stride;
}

} // namespace Sensor
