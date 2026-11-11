/**
 ******************************************************************************
 * @file    MagSubSensorFieldRaw.cpp
 * @brief   SubSensor for the uncorrected magnetic field.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Mag.Sub.Raw"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Magnetometer/MagSubSensorFieldRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticFieldRaw.hpp"

#include <limits>

namespace Sensor
{

MagSubSensorFieldRaw::MagSubSensorFieldRaw(
        MagSubSensorFieldRaw::DataProvider& provider,
        Sensor::ISensorDriverCtrl& ctrl)
    : mProvider(provider)
    , mDriver(*this,
              SDK::Sensor::Type::MAGNETIC_FIELD_RAW,
              SDK::SensorDataParser::MagneticFieldRaw::getFieldsNumber(),
              ctrl)
    , mQueue()
{
}

bool MagSubSensorFieldRaw::init()
{
    return mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& MagSubSensorFieldRaw::getDriver()
{
    return mDriver;
}

bool MagSubSensorFieldRaw::isActive()
{
    return mActive;
}

float MagSubSensorFieldRaw::start(float period)
{
    LOG_INFO("start\n");

    // Anything left was measured before this consumer existed.
    mQueue.clear();
    mDropped      = false;
    mDropReported = false;

    mActive   = true;
    mPeriodMs = period;

    if (!mProvider.requestRawFieldPeriod(static_cast<uint32_t>(period))) {
        // Refused, so the layer declines the connection.
        mActive = false;

        return 0.0f;
    }

    return period;
}

void MagSubSensorFieldRaw::stop()
{
    LOG_INFO("stop\n");

    // Cleared before the part is told to stop, so that whatever is still in
    // flight finds the sub-sensor closed.
    mActive = false;
    mQueue.clear();

    // The sub-sensor is closed either way.
    (void) mProvider.requestRawFieldPeriod(std::numeric_limits<uint32_t>::max());
}

float MagSubSensorFieldRaw::updatePeriod(float period)
{
    mPeriodMs = period;

    // A stream that is not up has no period to report as applied.
    if (!mProvider.requestRawFieldPeriod(static_cast<uint32_t>(period))) {
        return 0.0f;
    }

    return period;
}

float MagSubSensorFieldRaw::getMinPeriod()
{
    // The part's own floor, not the corrected channel's: this channel exists
    // for consumers that want every measurement.
    return mProvider.getMinRawFieldPeriod();
}

float MagSubSensorFieldRaw::getCurrentPeriod()
{
    return mPeriodMs;
}

const char* MagSubSensorFieldRaw::getDescription()
{
    return "Magnetic field raw";
}

void MagSubSensorFieldRaw::sensorRefresh()
{
    if (!mActive) {
        return;
    }

    // Everything accumulated since the last sweep, oldest first; thinning is
    // done downstream. Counted once rather than drained until empty, since
    // the part keeps adding while this runs.
    const uint32_t count = mQueue.size();

    Sample measured{};

    for (uint32_t idx = 0; idx < count; ++idx) {
        if (!mQueue.pop(measured, 0)) {
            break;
        }

        auto& sample = mDriver.getDataSample();

        // Counted against the step it was taken at.
        mDriver.setSourceStepUs(mSteps.stepAt(measured.timestampUs));

        // As measured; every consumer's rate is judged on this.
        sample.setTimestampUs(measured.timestampUs);
        sample.f[SDK::SensorDataParser::MagneticFieldRaw::MAG_X] = measured.x;
        sample.f[SDK::SensorDataParser::MagneticFieldRaw::MAG_Y] = measured.y;
        sample.f[SDK::SensorDataParser::MagneticFieldRaw::MAG_Z] = measured.z;

        mDriver.pushDataSample();
    }

    // Reported once per episode of the part outrunning the whole buffer.
    if (mDropped) {
        mDropped = false;

        if (!mDropReported) {
            mDropReported = true;

            LOG_WARNING("Sweep fell behind, measurements lost past %u\n",
                    static_cast<unsigned>(mQueue.capacity()));
        }
    } else {
        mDropReported = false;
    }
}

void MagSubSensorFieldRaw::addNewData(const Sample* data, size_t count, uint32_t periodUs)
{
    // Not held for a later consumer: they would predate it.
    if ((data == nullptr) || !mActive) {
        return;
    }

    // Noted by the stamp it holds from, not reported here: measurements still
    // queued from before a rate change are published under the step they
    // were taken at.
    if (count != 0) {
        mSteps.note(data[0].timestampUs, periodUs);
    }

    for (size_t idx = 0; idx < count; ++idx) {
        if (mQueue.available() == 0) {
            // Room made by the producer rather than waiting; the oldest goes.
            Sample displaced{};

            (void)mQueue.pop(displaced, 0);

            mDropped = true;
        }

        (void) mQueue.push(data[idx], 0);
    }
}

} // namespace Sensor
