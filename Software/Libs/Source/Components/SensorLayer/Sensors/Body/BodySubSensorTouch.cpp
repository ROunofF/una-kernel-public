/**
 ******************************************************************************
 * @file    BodySubSensorTouch.cpp
 * @brief   Touch (worn / unworn) sub-sensor
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Body.Sub.Touch"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Body/BodySubSensorTouch.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"

namespace Sensor
{

BodySubSensorTouch::BodySubSensorTouch(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::TOUCH_DETECT,
              SDK::SensorDataParser::Touch::getFieldsNumber(),
              ctrl,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mQueue.init(LOG_MODULE_PRX);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& BodySubSensorTouch::getDriver()
{
    return mDriver;
}

bool BodySubSensorTouch::isActive()
{
    return mActive;
}

float BodySubSensorTouch::start(float period)
{
    LOG_INFO("start\n");

    mActive = true;

    return period;
}

void BodySubSensorTouch::stop()
{
    LOG_INFO("stop\n");

    mActive = false;
}

const char* BodySubSensorTouch::getDescription()
{
    return "Touch detect sensor";
}

void BodySubSensorTouch::newConnection()
{
    OS::MutexCS cs(mDataMutex);
    publishData(TouchEvent{ mTouched, OS::GetTimeMs() });
}

void BodySubSensorTouch::sensorRefresh()
{
    TouchEvent event;
    while (mQueue.pop(event, 0)) {
        publishData(event);
    }
}

void BodySubSensorTouch::setTouch(bool value)
{
    if (!isActive()) {
        return;
    }

    OS::MutexCS cs(mDataMutex);
    LOG_DEBUG("rx touch=%u\n", static_cast<uint32_t>(value));
    if (mTouched != value) {
        mTouched = value;
        mQueue.push(TouchEvent{ value, OS::GetTimeMs() });
        Sensor::Manager::getInstance().wake();
    }
}

void BodySubSensorTouch::publishData(const TouchEvent& event)
{
    LOG_DEBUG("tx touch=%u\n", static_cast<uint32_t>(event.touched));
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(event.timestamp);
    sample.u[SDK::SensorDataParser::Touch::Field::TOUCH] = event.touched;
    mDriver.pushDataSample();
}

} // namespace Sensor
