/**
 ******************************************************************************
 * @file    GpsSubSensorAltimeter.cpp
 * @brief   SubSensor for the GPS Altimeter
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Gps.Sub.Alt"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Gps/GpsSubSensorAltimeter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp"
#include "System.hpp"

namespace Sensor
{

GpsSubSensorAltimeter::GpsSubSensorAltimeter(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::ALTIMETER,
              SDK::SensorDataParser::Altimeter::getFieldsNumber(),
              ctrl)
    , mTick()
    , mData()
    , mDataMutex()
{
    mDriver.setSourceTick(&mTick);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& GpsSubSensorAltimeter::getDriver()
{
    return mDriver;
}

bool GpsSubSensorAltimeter::isActive()
{
    return mTick.isActive();
}

float GpsSubSensorAltimeter::start(float period)
{
    LOG_INFO("start GPS Location\n");
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

void GpsSubSensorAltimeter::stop()
{
    LOG_INFO("stop GPS Location\n");
    mTick.stop();
    memset(&mData, 0, sizeof(mData));
}

float GpsSubSensorAltimeter::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float GpsSubSensorAltimeter::getMinPeriod()
{
    return 1000.0f;
}

float GpsSubSensorAltimeter::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* GpsSubSensorAltimeter::getDescription()
{
    return "Gps altimeter";
}

void GpsSubSensorAltimeter::sensorRefresh()
{
    if (mDriver.tickDue()) {
        LOG_DEBUG("ENTRY\n");

        OS::MutexCS cs(mDataMutex);

        auto& sample = mDriver.getDataSample();
        sample.setTimestamp(::Driver::System::GetTimeMs());
        sample.f[SDK::SensorDataParser::Altimeter::Field::ALTITUDE] = mData;
        mDriver.pushDataSample();

        LOG_DEBUG("EXIT\n");
    }
}

void GpsSubSensorAltimeter::setAltitude(float value)
{
    if (!mTick.isActive()) {
        return;
    }

    OS::MutexCS cs(mDataMutex);

    mData = value;
}

} // namespace Sensor
