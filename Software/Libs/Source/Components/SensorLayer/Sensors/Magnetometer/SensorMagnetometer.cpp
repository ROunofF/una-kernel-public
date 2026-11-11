/**
 ******************************************************************************
 * @file    SensorMagnetometer.cpp
 * @brief   Magnetometer SensorLayer binding.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.Mag"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Magnetometer/SensorMagnetometer.hpp"

#include <cassert>
#include <limits>

namespace Sensor
{

Magnetometer::Magnetometer()
    : mSubSensorFieldRaw(*this, *this)
    , mSubSensorField(mSubSensorFieldRaw.getDriver(), *this)
{
}

bool Magnetometer::init(Interface::IMagnetometer& magnetometer,
        Interface::IMagCorrection& calibration)
{
    if (!mSubSensorFieldRaw.init()) {
        return false;
    }

    if (!magnetometer.init()) {
        return false;
    }

    mpMagnetometer = &magnetometer;

    // The correction is applied in the sensor layer, not by the part.
    mSubSensorField.setCalibrator(&calibration);

    // Last, and only once the part is up: registering is taking it.
    magnetometer.setDataCallback(this);

    return true;
}

Sensor::Driver& Magnetometer::getDriverField()
{
    return mSubSensorField.getDriver();
}

Sensor::Driver& Magnetometer::getDriverFieldRaw()
{
    return mSubSensorFieldRaw.getDriver();
}

float Magnetometer::sdcStart(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->start(period);
}

void Magnetometer::sdcStop(Sensor::Driver* driver)
{
    getSubSensor(driver)->stop();
}

float Magnetometer::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->updatePeriod(period);
}

float Magnetometer::sdcGetMinPeriod(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getMinPeriod();
}

const char* Magnetometer::sdcGetDescription(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getDescription();
}

Sensor::ISubSensor* Magnetometer::getSubSensor(Sensor::Driver* driver)
{
    if (driver == &mSubSensorFieldRaw.getDriver()) {
        return &mSubSensorFieldRaw;
    }

    assert(driver == &mSubSensorField.getDriver());

    return &mSubSensorField;
}

bool Magnetometer::requestRawFieldPeriod(uint32_t value)
{
    // Sent on every call: a start or a stop is harmless to repeat, so no
    // state is kept to decide whether to send.
    if (value == std::numeric_limits<uint32_t>::max()) {
        return mpMagnetometer->requestStop();
    }

    // The part is asked in hertz. A rate outside what the part offers is
    // satisfied by the nearest rung, so there is nothing to clamp.
    const float rate = (value > 0U) ? (1000.0f / static_cast<float>(value))
                                    : mpMagnetometer->getMaxSampleRateHz();

    return mpMagnetometer->requestStart(rate);
}

float Magnetometer::getMinRawFieldPeriod()
{
    return 1000.0f / mpMagnetometer->getMaxSampleRateHz();
}

void Magnetometer::onMagnetic(uint64_t timestampUs, float x, float y, float z,
                              uint32_t periodUs)
{
    // Straight through: the sub-sensor's queue takes the sample without
    // waiting.
    const MagSubSensorFieldRaw::Sample measured { timestampUs, x, y, z };

    mSubSensorFieldRaw.addNewData(&measured, 1, periodUs);
}

} // namespace Sensor
