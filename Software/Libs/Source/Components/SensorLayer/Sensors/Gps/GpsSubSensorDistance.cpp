/**
 ******************************************************************************
 * @file    GpsSubSensorDistance.cpp
 * @brief   SubSensor for the GPS Distance
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Gps.Sub.Distance"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Gps/GpsSubSensorDistance.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp"
#include "System.hpp"
#include "ComponentFactory.hpp"

namespace Sensor
{

GpsSubSensorDistance::GpsSubSensorDistance(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::GPS_DISTANCE,
              SDK::SensorDataParser::GpsDistance::getFieldsNumber(),
              ctrl)
    , mTick()
    , mConnection(SDK::Sensor::Type::GPS_LOCATION, this)
    , mDistance(0.0f)
    , mGpsPoint0()
    , mGpsPoint0Valid(false)
    , mPoints()

{
    mDriver.setSourceTick(&mTick);
    mPoints.init(LOG_MODULE_PRX);
}

Sensor::Driver& GpsSubSensorDistance::getDriver()
{
    return mDriver;
}

bool GpsSubSensorDistance::isActive()
{
    return mTick.isActive();
}

float GpsSubSensorDistance::start(float period)
{
    LOG_INFO("start GPS Distance\n");

    mGpsPoint0Valid = false;
    mPoints.clear();

    mConnection.connect();

    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());

    return period;
}

void GpsSubSensorDistance::stop()
{
    LOG_INFO("stop GPS Distance\n");
    mConnection.disconnect();
    mTick.stop();

    // Forget the last reference point so the accumulated distance does not
    // jump on the next start. mDistance is kept as a since-boot odometer.
    mGpsPoint0Valid = false;
    mPoints.clear();
}

float GpsSubSensorDistance::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float GpsSubSensorDistance::getMinPeriod()
{
    return 1000.0f;
}

float GpsSubSensorDistance::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* GpsSubSensorDistance::getDescription()
{
    return "Gps distance";
}

void GpsSubSensorDistance::sensorRefresh()
{
    if (mDriver.tickDue()) {
        LOG_DEBUG("ENTRY\n");

        GpsSubSensorDistance::GpsPoint point;
        while (mPoints.pop(point, 0)) {
            if (mGpsPoint0Valid) {
                mDistance += distanceHaversine(mGpsPoint0, point);
            } else {
                mGpsPoint0Valid = true;
            }
            mGpsPoint0 = point;
        }

        auto& sample = mDriver.getDataSample();
        sample.setTimestamp(::Driver::System::GetTimeMs());
        sample.f[SDK::SensorDataParser::GpsDistance::Field::DISTANCE] = mDistance;
        mDriver.pushDataSample();

        LOG_DEBUG("EXIT\n");
    }
}

void GpsSubSensorDistance::onSdlNewData(uint16_t                 handle,
                                        const SDK::Sensor::Data* data,
                                        uint16_t                 count,
                                        uint16_t                 stride)
{
    SDK::Sensor::DataBatch batch(data, count, stride);

    if (mConnection.matchesDriver(handle)) {
        SDK::SensorDataParser::GpsLocation p(batch[0]);

        if (p.isCoordinatesValid()) {
            GpsSubSensorDistance::GpsPoint point(p.getLatitude(), p.getLongitude());
            mPoints.push(point, 0);
        }
    }
}


} // namespace Sensor
