/**
 ******************************************************************************
 * @file    SensorGps.cpp
 * @brief   Sensor for the GPS
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "SensorGPS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Gps/SensorGps.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "System.hpp"
#include "OS.hpp"
#include "ComponentFactory.hpp"

namespace Sensor
{

Gps::Gps()
    : mGps(ComponentFactory::GetInstance().getGps())
    , mLocation(*this)
    , mSpeed(*this)
    , mDistance(*this)
    , mAltimeter(*this)
    , mGpsPeriod(1000)
    , mSubSensors{ { &mLocation, &mSpeed, &mDistance, &mAltimeter } }
{
    mGps.attachCallback(this);

    mGps.setPeriod(static_cast<uint32_t>(mGpsPeriod / 1000.0f));
}

Sensor::Driver& Gps::getDriverLocation()
{
    return mLocation.getDriver();
}

Sensor::Driver& Gps::getDriverSpeed()
{
    return mSpeed.getDriver();
}

Sensor::Driver& Gps::getDriverDistance()
{
    return mDistance.getDriver();
}

Sensor::Driver& Gps::getDriverAltimeter()
{
    return mAltimeter.getDriver();
}

float Gps::sdcStart(Sensor::Driver* driver, float period)
{
    period = getSubSensor(driver)->start(period);

    if (period > 0) {
        if (getActiveCount() == 1) {
            LOG_INFO("start GPS chip\n");
            mGps.enable();
        }
    }

    return period;
}

void Gps::sdcStop(Sensor::Driver* driver)
{
    getSubSensor(driver)->stop();

    if (getActiveCount() == 0) {
        LOG_INFO("stop GPS chip\n");
        mGps.disable();
    }
}

float Gps::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    float res = getSubSensor(driver)->updatePeriod(period);

    if (res <= 0) {
        return res;
    }

    updateGpsPeriod();

    return res;
}

float Gps::sdcGetMinPeriod(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getMinPeriod();
}

const char* Gps::sdcGetDescription(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getDescription();
}

void Gps::onGpsLocation(Interface::IGps::LocationInfo location)
{
    mLocation.setLocation(location);
}

uint8_t Gps::getActiveCount()
{
    uint8_t summa = 0;

    for (auto s : mSubSensors) {
        if (s->isActive()) {
            ++summa;
        }
    }

    return summa;
}

float Gps::getMinPeriod()
{
    float period = mSubSensors[0]->getCurrentPeriod();

    for (uint8_t idx = 1; idx < mSubSensors.size(); ++idx) {
        if (period > mSubSensors[idx]->getCurrentPeriod()) {
            period = mSubSensors[idx]->getCurrentPeriod();
        }
    }

    return period;
}

void Gps::updateGpsPeriod()
{
    const float minPeriod = getMinPeriod();
    const float period    = minPeriod <= 1000.0f ? 1000.0f : minPeriod;

    if (period != mGpsPeriod) {
        mGpsPeriod = period;
        mGps.setPeriod(static_cast<uint32_t>(mGpsPeriod / 1000.0f));
    }
}

Sensor::ISubSensor* Gps::getSubSensor(Sensor::Driver* driver)
{
    for (auto s : mSubSensors) {
        if (&s->getDriver() == driver) {
            return s;
        }
    }

    assert(false);

    return nullptr;
}

} /* namespace Sensor */
