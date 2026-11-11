/**
 ******************************************************************************
 * @file    ImuSubSensorFusion.cpp
 * @brief   SubSensor for the Fusion
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "IMU.Sub.Fusion"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Imu/ImuSubSensorFusion.hpp"
#include "ImuFifoCadence.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusion.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"
#include "System.hpp"

#include <algorithm>

namespace Sensor
{
ImuSubSensorFusion::ImuSubSensorFusion(Sensor::Driver&            driverFusionRaw,
                                       Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::FUSION,
              SDK::SensorDataParser::Fusion::getFieldsNumber(),
              ctrl)
    , mTimer()
    , mDriverFusionRaw(driverFusionRaw)
    , mQueue()
    , mPeriod(0.0f)
    , mPeriodUs(0)
    , mLatency(kDefaultLatency)
{
    mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& ImuSubSensorFusion::getDriver()
{
    return mDriver;
}

bool ImuSubSensorFusion::isActive()
{
    return mTimer.getActive();
}

float ImuSubSensorFusion::start(float period)
{
    LOG_DEBUG("start\n");

    mTimer.start(period);

    mPeriod = period;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mDriverFusionRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    return period;
}

void ImuSubSensorFusion::stop()
{
    LOG_DEBUG("stop\n");

    mTimer.stop();

    mDriverFusionRaw.disconnect(this);
}

float ImuSubSensorFusion::updatePeriod(float period)
{
    mPeriod = period;

    // The raw channel hands over, from inside this call, what its queue for
    // this sensor still holds: readings thinned to the period this sensor
    // had, which is the one their step is worked out from until the call is
    // back.
    mDriverFusionRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));

    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    mTimer.start(period);

    return period;
}

void ImuSubSensorFusion::updateLatency(uint32_t latency)
{
    mLatency.store((latency != 0) ? latency : kDefaultLatency, std::memory_order_relaxed);

    if (isActive()) {
        mDriverFusionRaw.connect(this, mPeriod, mLatency.load(std::memory_order_relaxed));
    }
}

float ImuSubSensorFusion::getMinPeriod()
{
    return mMinPeriod;
}

float ImuSubSensorFusion::getCurrentPeriod()
{
    return static_cast<float>(mTimer.getInterval());
}

const char* ImuSubSensorFusion::getDescription()
{
    return "BMI270 Fusion";
}

void ImuSubSensorFusion::sensorRefresh()
{
    // Readings are handed on as soon as they are here. The source delivers
    // them in batches of its own; waiting for a timer on top of that would
    // only hold each batch back.
    const size_t count = mQueue.size();

    if (count != 0) {
        LOG_DEBUG("ENTRY\n");

        ImuSubSensorFusion::Data data{};
        for (size_t idx = 0; idx < count && mQueue.pop(data, 0); ++idx) {
            Sensor::DataSample& sample = mDriver.getDataSample();
            // Counted against the step it was taken at.
            mDriver.setSourceStepUs(mSteps.stepAt(data.ts));
            sample.setTimestampUs(data.ts);
            sample.f[SDK::SensorDataParser::Fusion::Field::ACCEL_X] = data.accelX;
            sample.f[SDK::SensorDataParser::Fusion::Field::ACCEL_Y] = data.accelY;
            sample.f[SDK::SensorDataParser::Fusion::Field::ACCEL_Z] = data.accelZ;
            sample.f[SDK::SensorDataParser::Fusion::Field::GYRO_X]  = data.gyroX;
            sample.f[SDK::SensorDataParser::Fusion::Field::GYRO_Y]  = data.gyroY;
            sample.f[SDK::SensorDataParser::Fusion::Field::GYRO_Z]  = data.gyroZ;
            mDriver.pushDataSample();
        }

        // The raw stream hands over within the latency asked of it, and no
        // more often than the FIFO behind it is read, however little is asked.
        mDriver.endOfBatch(std::max(mLatency.load(std::memory_order_relaxed),
                                    ImuFifoCadence::kMinFifoLatencyMs));

        LOG_DEBUG("EXIT\n");
    }
}

void ImuSubSensorFusion::onSdlNewData(uint16_t                 handle,
                                      const SDK::Sensor::Data* base,
                                      uint16_t                 count,
                                      uint16_t                 stride)
{
    (void) handle;

    SDK::Sensor::DataBatch batch(base, count, stride);

    if (mQueue.available() < batch.size()) {
        LOG_ERROR("Fusion queue has no enough space\n");
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

        mSteps.note(tsUs, SampleRateAdapter::outputStepUs(periodUs, mDriverFusionRaw.getSourceStepUsAt(tsUs)));
    }

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        SDK::SensorDataParser::FusionRaw       p(batch[idx]);
        SDK::SensorDataParser::FusionRaw::Data fusionRawData;

        if (p.getData(fusionRawData)) {
            ImuSubSensorFusion::Data fusionData{};

            fusionData.ts     = p.getTimestampUs();
            fusionData.accelX = fusionRawData.accel.x * mScaleAccel;
            fusionData.accelY = fusionRawData.accel.y * mScaleAccel;
            fusionData.accelZ = fusionRawData.accel.z * mScaleAccel;
            fusionData.gyroX  = fusionRawData.gyro.x  * mScaleGyro;
            fusionData.gyroY  = fusionRawData.gyro.y  * mScaleGyro;
            fusionData.gyroZ  = fusionRawData.gyro.z  * mScaleGyro;

            mQueue.push(fusionData);
        }
    }
}

} // namespace Sensor
