/**
 ******************************************************************************
 * @file    ImuSubSensorAccel.cpp
 * @brief   SubSensor for the accelerometer
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.Accel"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorAccel.hpp"
#include "ImuFifoCadence.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"
#include "System.hpp"

#include <algorithm>

namespace Sensor
{
ImuSubSensorAccel::ImuSubSensorAccel(Sensor::Driver&            driverAccRaw,
                                     Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::ACCELEROMETER,
              SDK::SensorDataParser::Accelerometer::getFieldsNumber(),
              ctrl)
    , mTimer()
    , mDriverAccRaw(driverAccRaw)
    , mQueue()
    , mPeriod(0.0f)
    , mPeriodUs(0)
    , mLatency(kDefaultLatency)
{
    mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorAccel::getDriver()
{
    return mDriver;
}

bool ImuSubSensorAccel::isActive()
{
    return mTimer.getActive();
}

float ImuSubSensorAccel::start(float period)
{
    LOG_DEBUG("start\n");

    mTimer.start(period);

    mPeriod = period;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mDriverAccRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    return period;
}

void ImuSubSensorAccel::stop()
{
    LOG_DEBUG("stop\n");

    mTimer.stop();

    mDriverAccRaw.disconnect(this);
}

float ImuSubSensorAccel::updatePeriod(float period)
{
    mPeriod = period;

    // The raw channel hands over, from inside this call, what its queue for
    // this sensor still holds: readings thinned to the period this sensor
    // had, which is the one their step is worked out from until the call is
    // back.
    mDriverAccRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mTimer.start(period);

    return period;
}

void ImuSubSensorAccel::updateLatency(uint32_t latency)
{
    mLatency.store((latency != 0) ? latency : kDefaultLatency, std::memory_order_relaxed);

    if (isActive()) {
        mDriverAccRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));
    }
}

float ImuSubSensorAccel::getMinPeriod()
{
    return 0.625f;
}

float ImuSubSensorAccel::getCurrentPeriod()
{
    return static_cast<float>(mTimer.getInterval());
}

const char* ImuSubSensorAccel::getDescription()
{
    return "BMI270 accelerometer";
}

void ImuSubSensorAccel::sensorRefresh()
{
    // Readings are handed on as soon as they are here. The source delivers
    // them in batches of its own; waiting for a timer on top of that would
    // only hold each batch back.
    const size_t count = mQueue.size();

    if (count != 0) {
        LOG_DEBUG("ENTRY\n");

        ImuSubSensorAccel::Data data{};
        for (size_t idx = 0; idx < count && mQueue.pop(data, 0); ++idx) {
            Sensor::DataSample& sample = mDriver.getDataSample();
            // Counted against the step it was taken at.
            mDriver.setSourceStepUs(mSteps.stepAt(data.ts));
            sample.setTimestampUs(data.ts);
            sample.f[SDK::SensorDataParser::Accelerometer::Field::X] = data.x;
            sample.f[SDK::SensorDataParser::Accelerometer::Field::Y] = data.y;
            sample.f[SDK::SensorDataParser::Accelerometer::Field::Z] = data.z;
            mDriver.pushDataSample();
        }

        // The raw stream hands over within the latency asked of it, and no
        // more often than the FIFO behind it is read, however little is asked.
        mDriver.endOfBatch(std::max(mLatency.load(std::memory_order_relaxed),
                                    ImuFifoCadence::kMinFifoLatencyMs));

        LOG_DEBUG("EXIT\n");
    }
}

void ImuSubSensorAccel::onSdlNewData(uint16_t                 handle,
                                     const SDK::Sensor::Data* base,
                                     uint16_t                 count,
                                     uint16_t                 stride)
{
    (void) handle;

    SDK::Sensor::DataBatch batch(base, count, stride);

    if (mQueue.available() < batch.size()) {
        LOG_ERROR("accelerometer queue has no enough space\n");
        return;
    }

    // What arrives is the raw stream as this sensor's own queue on it
    // decimated it, so its step is that decimation's output step: noted by
    // the stamp it holds from, as the readings wait here to be published.
    // Reading by reading: a delivery can hold some taken before the raw
    // channel changed its rate and some after.
    const uint64_t periodUs = mPeriodUs.load(std::memory_order_relaxed);

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        const uint64_t tsUs = batch[idx].getTimestampUs();

        mSteps.note(tsUs, SampleRateAdapter::outputStepUs(periodUs, mDriverAccRaw.getSourceStepUsAt(tsUs)));
    }

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        SDK::SensorDataParser::AccelerometerRaw p(batch[idx]);

        Sensor::ImuSubSensorAccel::Data xlData{};

        xlData.ts = p.getTimestampUs();
        xlData.x  = p.getX() * mScale;
        xlData.y  = p.getY() * mScale;
        xlData.z  = p.getZ() * mScale;

        mQueue.push(xlData);
    }
}

} // namespace Sensor
