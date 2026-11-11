/**
 ******************************************************************************
 * @file    SensorBarometer.cpp
 * @brief   Barometer SensorLayer binding.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.Baro"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Barometer/SensorBarometer.hpp"

#include "System.hpp"

#include <cassert>

namespace Sensor
{

Barometer::Barometer()
    : mSubSensorP(BaroSubSensor::Channel::PRESSURE, *this, *this)
    , mSubSensorT(BaroSubSensor::Channel::TEMPERATURE, *this, *this)
{
}

bool Barometer::init(Interface::IBarometer& barometer)
{
    if (!mMutex.init(LOG_MODULE_PRX)) {
        return false;
    }

    if (!mSubSensorP.init() || !mSubSensorT.init()) {
        return false;
    }

    if (!barometer.init()) {
        return false;
    }

    mpBarometer = &barometer;

    // Last, and only once the part is up: registering is taking it.
    barometer.setDataCallback(this);

    return true;
}

Sensor::Driver& Barometer::getDriverTemp()
{
    return mSubSensorT.getDriver();
}

Sensor::Driver& Barometer::getDriverPressure()
{
    return mSubSensorP.getDriver();
}

void Barometer::computeP0(float altitude)
{
    if (mpBarometer == nullptr) {
        return;
    }

    OS::MutexCS lock(mMutex);

    if (!mP0Adjuster.start(altitude, ::Driver::System::GetTimeMs())) {
        return;
    }

    (void) apply();
}

float Barometer::sdcStart(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->start(period);
}

void Barometer::sdcStop(Sensor::Driver* driver)
{
    getSubSensor(driver)->stop();
}

float Barometer::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->updatePeriod(period);
}

float Barometer::sdcGetMinPeriod(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getMinPeriod();
}

const char* Barometer::sdcGetDescription(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getDescription();
}

Sensor::ISubSensor* Barometer::getSubSensor(Sensor::Driver* driver)
{
    if (driver == &mSubSensorP.getDriver()) {
        return &mSubSensorP;
    }

    assert(driver == &mSubSensorT.getDriver());

    return &mSubSensorT;
}

bool Barometer::requestPeriod(BaroSubSensor::Channel channel, uint32_t value)
{
    OS::MutexCS lock(mMutex);

    uint32_t& wanted = mPeriodMs[static_cast<uint8_t>(channel)];

    const uint32_t before = wanted;

    wanted = value;

    if (apply()) {
        return true;
    }

    // A period the part did not take is not kept: the channel declines its
    // consumer, and the next request must not start the part for it. A
    // release stands either way, the channel being closed.
    if (value != skNone) {
        wanted = before;
    }

    return false;
}

float Barometer::getMinPeriod()
{
    return 1000.0f / mpBarometer->getMaxSampleRateHz();
}

bool Barometer::apply()
{
    uint32_t periodMs = (mPeriodMs[0] < mPeriodMs[1]) ? mPeriodMs[0] : mPeriodMs[1];

    // A calculation the pressure stopped coming for is not left to hold
    // the part at its fastest, or on at all.
    (void) mP0Adjuster.expire(::Driver::System::GetTimeMs());

    // The calculation averages a short run and wants it over with: it asks
    // for the fastest the part has, with a consumer or without one.
    if (mP0Adjuster.isActive()) {
        periodMs = 0U;
    }

    if (periodMs == skNone) {
        return mpBarometer->requestStop();
    }

    // The part is asked in hertz. A rate outside what the part offers is
    // satisfied by the nearest rung, so there is nothing to clamp.
    const float rate = (periodMs > 0U) ? (1000.0f / static_cast<float>(periodMs))
                                       : mpBarometer->getMaxSampleRateHz();

    return mpBarometer->requestStart(rate);
}

void Barometer::onPressure(uint64_t timestampUs, float mbar, uint32_t periodUs)
{
    BaroSubSensor::Sample measured { timestampUs, mbar, 0.0f };

    {
        OS::MutexCS lock(mMutex);

        if (mP0Adjuster.isActive()) {
            if (mP0Adjuster.update(mbar,
                    static_cast<uint32_t>(timestampUs / 1000U))) {
                mSeaLevelPa = mP0Adjuster.getP0();
            }

            // Done, with a result or without: the part goes back to what the
            // channels ask for, or off.
            if (!mP0Adjuster.isActive()) {
                (void) apply();
            }
        }

        measured.seaLevelPa = mSeaLevelPa;
    }

    // Straight through: the sub-sensor's queue takes the sample without
    // waiting.
    mSubSensorP.addNewData(measured, periodUs);
}

void Barometer::onTemperature(uint64_t timestampUs, float degC, uint32_t periodUs)
{
    const BaroSubSensor::Sample measured { timestampUs, degC, 0.0f };

    mSubSensorT.addNewData(measured, periodUs);
}

} // namespace Sensor
