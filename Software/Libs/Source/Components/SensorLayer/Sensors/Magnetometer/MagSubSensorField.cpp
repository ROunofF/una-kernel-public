/**
 ******************************************************************************
 * @file    MagSubSensorField.cpp
 * @brief   SubSensor for the magnetic field.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Mag.Sub.Field"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Magnetometer/MagSubSensorField.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticField.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticFieldRaw.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"

namespace Sensor
{

MagSubSensorField::MagSubSensorField(Sensor::Driver& driverFieldRaw,
        Sensor::ISensorDriverCtrl& ctrl)
    : mDriverFieldRaw(driverFieldRaw)
    , mDriver(*this,
              SDK::Sensor::Type::MAGNETIC_FIELD,
              SDK::SensorDataParser::MagneticField::getFieldsNumber(),
              ctrl)
{
}

void MagSubSensorField::setCalibrator(
        const Interface::IMagCorrection* pCalibrator)
{
    mpCalibrator = pCalibrator;
}

Sensor::Driver& MagSubSensorField::getDriver()
{
    return mDriver;
}

bool MagSubSensorField::isActive()
{
    return mActive;
}

float MagSubSensorField::start(float period)
{
    LOG_INFO("start\n");

    mActive   = true;
    mPeriodMs = period;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    // Subscribing starts the raw channel. No latency asked for: one sample
    // per delivery, since batching would only delay a bearing.
    if (!mDriverFieldRaw.connect(this, period, 0)) {
        // Refused, so the layer does not register a consumer nothing feeds.
        mActive = false;

        return 0.0f;
    }

    return period;
}

void MagSubSensorField::stop()
{
    LOG_INFO("stop\n");

    mActive = false;

    mDriverFieldRaw.disconnect(this);
}

float MagSubSensorField::updatePeriod(float period)
{
    // The raw channel renegotiates an existing listener in place. Refused,
    // it goes on feeding this sensor at the period it had, so that period
    // stays the one the step of this channel is worked out from.
    if (!mDriverFieldRaw.connect(this, period, 0)) {
        return 0.0f;
    }

    mPeriodMs = period;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)), std::memory_order_relaxed);

    return period;
}

float MagSubSensorField::getMinPeriod()
{
    return static_cast<float>(skMinPeriod);
}

float MagSubSensorField::getCurrentPeriod()
{
    return mPeriodMs;
}

const char* MagSubSensorField::getDescription()
{
    return "Magnetic field";
}

void MagSubSensorField::sensorRefresh()
{
    // Nothing to poll: samples are published from inside the raw channel's
    // delivery.
}

void MagSubSensorField::onSdlNewData(uint16_t                 handle,
        const SDK::Sensor::Data* base, uint16_t count, uint16_t stride)
{
    (void) handle;

    // A delivery can still be in flight across a stop.
    if (!mActive) {
        return;
    }

    // What arrives is the raw channel as this sensor's own queue on it
    // decimated it, so its step is that decimation's output step.
    mDriver.setSourceStepUs(SampleRateAdapter::outputStepUs(
            mPeriodUs.load(std::memory_order_relaxed), mDriverFieldRaw.getSourceStepUs()));

    SDK::Sensor::DataBatch batch(base, count, stride);

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        const SDK::SensorDataParser::MagneticFieldRaw measured(batch[idx]);

        float x = measured.getX();
        float y = measured.getY();
        float z = measured.getZ();

        // One call, so that a calibration landing mid-stream either applies
        // to this sample and is reported, or does neither. Nothing to apply
        // is not an error: the sample stays as measured, flagged so.
        const bool calibrated = (mpCalibrator != nullptr)
                && mpCalibrator->apply(x, y, z);

        auto& sample = mDriver.getDataSample();

        // When the field was measured; every consumer's rate is judged on it.
        sample.setTimestampUs(measured.getTimestampUs());
        sample.f[SDK::SensorDataParser::MagneticField::MAG_X] = x;
        sample.f[SDK::SensorDataParser::MagneticField::MAG_Y] = y;
        sample.f[SDK::SensorDataParser::MagneticField::MAG_Z] = z;
        sample.u[SDK::SensorDataParser::MagneticField::MAG_CALIBRATED] =
                calibrated ? 1U : 0U;

        mDriver.pushDataSample();
    }
}

} // namespace Sensor
