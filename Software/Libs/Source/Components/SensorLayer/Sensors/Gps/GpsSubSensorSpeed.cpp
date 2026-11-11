/**
 ******************************************************************************
 * @file    GpsSubSensorSpeed.cpp
 * @brief   SubSensor for the GPS Speed
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Gps.Sub.Speed"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Gps/GpsSubSensorSpeed.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp"
#include "System.hpp"
#include "ComponentFactory.hpp"

namespace Sensor
{

GpsSubSensorSpeed::GpsSubSensorSpeed(Sensor::ISensorDriverCtrl& ctrl)
    : mGps(ComponentFactory::GetInstance().getGps())
    , mDriver(*this,
              SDK::Sensor::Type::GPS_SPEED,
              SDK::SensorDataParser::GpsSpeed::getFieldsNumber(),
              ctrl)
    , mTick()
{
    mDriver.setSourceTick(&mTick);
}

Sensor::Driver& GpsSubSensorSpeed::getDriver()
{
    return mDriver;
}

bool GpsSubSensorSpeed::isActive()
{
    return mTick.isActive();
}

float GpsSubSensorSpeed::start(float period)
{
    LOG_INFO("start GPS Speed\n");
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

void GpsSubSensorSpeed::stop()
{
    LOG_INFO("stop GPS Speed\n");
    mTick.stop();
}

float GpsSubSensorSpeed::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float GpsSubSensorSpeed::getMinPeriod()
{
    return 1000.0f;
}

float GpsSubSensorSpeed::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* GpsSubSensorSpeed::getDescription()
{
    return "Gps speed";
}

void GpsSubSensorSpeed::sensorRefresh()
{
    if (mDriver.tickDue()) {
        LOG_DEBUG("ENTRY\n");

        auto& sample = mDriver.getDataSample();
        sample.setTimestamp(::Driver::System::GetTimeMs());
        sample.f[SDK::SensorDataParser::GpsSpeed::Field::SPEED] = mGps.getSpeed();
        // SPEED_VALID means a reliable current fix: a real fix (autonomous,
        // differential/DGPS, or precise/RTK - not only GGA GPS_FIX) that is NOT
        // dead-reckoning. The chipset can report a fix while RMC mode is
        // ESTIMATED, whose extrapolated speed is unreliable.
        const Interface::IGps::FixMode mode = mGps.getFixMode();
        const bool deadReckoning = (mode == Interface::IGps::FixMode::ESTIMATED);
        // Only fall back to the GGA-derived hasFix() when the RMC fix mode is
        // not yet known (UNKNOWN). For explicit non-fix modes (NOT_VALID,
        // MANUAL, SIMULATED, ESTIMATED) a stale GGA position must NOT promote
        // the sample to valid.
        const bool realFix =
            mode == Interface::IGps::FixMode::AUTONOMOUS ||
            mode == Interface::IGps::FixMode::DIFFERENTIAL ||
            mode == Interface::IGps::FixMode::PRECISE ||
            (mode == Interface::IGps::FixMode::UNKNOWN && mGps.hasFix());
        sample.u[SDK::SensorDataParser::GpsSpeed::Field::SPEED_VALID] =
            (realFix && !deadReckoning) ? 1u : 0u;
        sample.u[SDK::SensorDataParser::GpsSpeed::Field::DEAD_RECKONING] =
            deadReckoning ? 1u : 0u;
        mDriver.pushDataSample();

        LOG_DEBUG("EXIT\n");
    }
}

} // namespace Sensor
