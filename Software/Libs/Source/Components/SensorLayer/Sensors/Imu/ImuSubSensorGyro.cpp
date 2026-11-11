/**
 ******************************************************************************
 * @file    ImuSubSensorGyro.cpp
 * @brief   SubSensor for the gyroscope
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.Gyro"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorGyro.hpp"
#include "ImuFifoCadence.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"
#include "System.hpp"

#include <algorithm>

namespace Sensor
{
ImuSubSensorGyro::ImuSubSensorGyro(Sensor::Driver&            driver,
                                   Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::GYROSCOPE,
              SDK::SensorDataParser::Gyroscope::getFieldsNumber(),
              ctrl)
    , mTimer()
    , mDriverRaw(driver)
    , mPeriod(0.0f)
    , mPeriodUs(0)
    , mLatency(kDefaultLatency)
{
    mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorGyro::getDriver()
{
    return mDriver;
}

bool ImuSubSensorGyro::isActive()
{
    return mTimer.getActive();
}

float ImuSubSensorGyro::start(float period)
{
    LOG_DEBUG("start\n");

    mTimer.start(period);

    mPeriod = period;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mDriverRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    return period;
}

void ImuSubSensorGyro::stop()
{
    LOG_DEBUG("stop\n");

    mTimer.stop();

    mDriverRaw.disconnect(this);
}

float ImuSubSensorGyro::updatePeriod(float period)
{
    mPeriod = period;

    // The raw channel hands over, from inside this call, what its queue for
    // this sensor still holds: readings thinned to the period this sensor
    // had, which is the one their step is worked out from until the call is
    // back.
    mDriverRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mTimer.start(period);

    return period;
}

void ImuSubSensorGyro::updateLatency(uint32_t latency)
{
    mLatency.store((latency != 0) ? latency : kDefaultLatency, std::memory_order_relaxed);

    if (isActive()) {
        mDriverRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));
    }
}

float ImuSubSensorGyro::getMinPeriod()
{
    return mMinPeriod;
}

float ImuSubSensorGyro::getCurrentPeriod()
{
    return static_cast<float>(mTimer.getInterval());
}

const char* ImuSubSensorGyro::getDescription()
{
    return "BMI270 gyroscope";
}

void ImuSubSensorGyro::sensorRefresh()
{
    // Readings are handed on as soon as they are here. The source delivers
    // them in batches of its own; waiting for a timer on top of that would
    // only hold each batch back.
    const size_t count = mQueue.size();

    if (count != 0) {
        LOG_DEBUG("ENTRY\n");

        ImuSubSensorGyro::Data data{};
        for (size_t idx = 0; idx < count && mQueue.pop(data, 0); ++idx) {
            Sensor::DataSample& sample = mDriver.getDataSample();
            // Counted against the step it was taken at.
            mDriver.setSourceStepUs(mSteps.stepAt(data.ts));
            sample.setTimestampUs(data.ts);
            sample.f[SDK::SensorDataParser::Gyroscope::Field::X] = data.x;
            sample.f[SDK::SensorDataParser::Gyroscope::Field::Y] = data.y;
            sample.f[SDK::SensorDataParser::Gyroscope::Field::Z] = data.z;
            mDriver.pushDataSample();
        }

        // The raw stream hands over within the latency asked of it, and no
        // more often than the FIFO behind it is read, however little is asked.
        mDriver.endOfBatch(std::max(mLatency.load(std::memory_order_relaxed),
                                    ImuFifoCadence::kMinFifoLatencyMs));

        LOG_DEBUG("EXIT\n");
    }
}

void ImuSubSensorGyro::onSdlNewData(uint16_t                 handle,
                                    const SDK::Sensor::Data* base,
                                    uint16_t                 count,
                                    uint16_t                 stride)
{
    (void) handle;

    SDK::Sensor::DataBatch batch(base, count, stride);

    if (mQueue.available() < batch.size()) {
        LOG_ERROR("gyroscope queue has no enough space\n");
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

        mSteps.note(tsUs, SampleRateAdapter::outputStepUs(periodUs, mDriverRaw.getSourceStepUsAt(tsUs)));
    }

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        SDK::SensorDataParser::GyroscopeRaw p(batch[idx]);

        Sensor::ImuSubSensorGyro::Data gyroData{};

        gyroData.ts = p.getTimestampUs();
        gyroData.x  = p.getX() * mScaleGyro;
        gyroData.y  = p.getY() * mScaleGyro;
        gyroData.z  = p.getZ() * mScaleGyro;

        mQueue.push(gyroData);
    }
}

} // namespace Sensor
