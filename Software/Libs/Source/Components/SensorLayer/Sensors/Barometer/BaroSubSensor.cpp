/**
 ******************************************************************************
 * @file    BaroSubSensor.cpp
 * @brief   SubSensor for one channel of the barometer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Baro.Sub"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Barometer/BaroSubSensor.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp"

#include <limits>

namespace Sensor
{

BaroSubSensor::BaroSubSensor(Channel channel, DataProvider& provider,
        Sensor::ISensorDriverCtrl& ctrl)
    : mChannel(channel)
    , mProvider(provider)
    , mDriver(*this,
              (channel == Channel::PRESSURE)
                      ? SDK::Sensor::Type::PRESSURE
                      : SDK::Sensor::Type::AMBIENT_TEMPERATURE,
              (channel == Channel::PRESSURE)
                      ? SDK::SensorDataParser::Pressure::getFieldsNumber()
                      : SDK::SensorDataParser::Temperature::getFieldsNumber(),
              ctrl)
    , mQueue()
{
}

bool BaroSubSensor::init()
{
    return mQueue.init(LOG_MODULE_PRX);
}

Sensor::Driver& BaroSubSensor::getDriver()
{
    return mDriver;
}

bool BaroSubSensor::isActive()
{
    return mActive;
}

float BaroSubSensor::start(float period)
{
    LOG_INFO("%s: start\n", getDescription());

    // Anything left was measured before this consumer existed.
    mQueue.clear();
    mDropped      = false;
    mDropReported = false;

    mActive   = true;
    mPeriodMs = period;

    if (!mProvider.requestPeriod(mChannel, static_cast<uint32_t>(period))) {
        // Refused, so the layer declines the connection.
        mActive = false;
        return 0.0f;
    }

    return period;
}

void BaroSubSensor::stop()
{
    LOG_INFO("%s: stop\n", getDescription());

    // Cleared before the part is told, so that whatever is still in flight
    // finds the sub-sensor closed.
    mActive = false;
    mQueue.clear();

    // The sub-sensor is closed either way.
    (void) mProvider.requestPeriod(mChannel, std::numeric_limits<uint32_t>::max());
}

float BaroSubSensor::updatePeriod(float period)
{
    mPeriodMs = period;

    // A stream that is not up has no period to report as applied.
    if (!mProvider.requestPeriod(mChannel, static_cast<uint32_t>(period))) {
        return 0.0f;
    }

    return period;
}

float BaroSubSensor::getMinPeriod()
{
    return mProvider.getMinPeriod();
}

float BaroSubSensor::getCurrentPeriod()
{
    return mPeriodMs;
}

const char* BaroSubSensor::getDescription()
{
    return (mChannel == Channel::PRESSURE) ? "Barometer pressure"
                                           : "Barometer temperature";
}

void BaroSubSensor::sensorRefresh()
{
    if (!mActive) {
        return;
    }

    // Everything accumulated since the last sweep, oldest first; thinning is
    // done downstream. Counted once rather than drained until empty, since
    // the part can add while this runs.
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

        if (mChannel == Channel::PRESSURE) {
            // Published in Pa.
            sample.f[SDK::SensorDataParser::Pressure::PRESS] =
                    measured.value * 100.0f;
            sample.f[SDK::SensorDataParser::Pressure::PRESS_SEA_LEVEL] =
                    measured.seaLevelPa;
        } else {
            sample.f[SDK::SensorDataParser::Temperature::TEMP] = measured.value;
        }

        mDriver.pushDataSample();
    }

    // Reported once per episode of the part outrunning the whole buffer.
    if (mDropped) {
        mDropped = false;

        if (!mDropReported) {
            mDropReported = true;
            LOG_WARNING("%s: sweep fell behind, measurements lost past %u\n",
                    getDescription(), static_cast<unsigned>(mQueue.capacity()));
        }
    } else {
        mDropReported = false;
    }
}

void BaroSubSensor::addNewData(const Sample& sample, uint32_t periodUs)
{
    // Not held for a later consumer: it would predate it.
    if (!mActive) {
        return;
    }

    // Noted by the stamp it holds from, not reported here: a measurement
    // still queued from before a rate change is published under the step it
    // was taken at.
    mSteps.note(sample.timestampUs, periodUs);

    if (mQueue.available() == 0) {
        // Room made by the producer rather than waiting; the oldest goes.
        Sample displaced{};
        (void) mQueue.pop(displaced, 0);
        mDropped = true;
    }

    (void) mQueue.push(sample, 0);

    // Handed on now rather than at the next refresh tick, which knows nothing
    // of the part's clock.
    Sensor::Manager::getInstance().wake();
}

} // namespace Sensor
