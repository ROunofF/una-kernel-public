/**
 ******************************************************************************
 * @file    SensorFloor.cpp
 * @brief   Virtual floor-counter sensor
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.Floor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Floor/SensorFloor.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp"

namespace Sensor
{

Floor::Floor()
    : mDriverStepDetector(SDK::Sensor::Type::STEP_DETECTOR, this)
    , mDriverPressure(SDK::Sensor::Type::PRESSURE, this)
    , mDriver(*this,
              SDK::Sensor::Type::FLOOR_COUNTER,
              SDK::SensorDataParser::FloorCounter::getFieldsNumber(),
              *this,
              Sensor::Driver::Mode::EVENT_BASED)
{
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& Floor::getDriver()
{
    return mDriver;
}

float Floor::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    mFloorCounter.resetFSM();

    // StepDetector is event-based: a requested period would be ignored.
    mDriverStepDetector.connect();
    mDriverPressure.connect(mPressurePeriod);

    return period;
}

void Floor::sdcStop(Sensor::Driver* /*driver*/)
{
    mDriverStepDetector.disconnect();
    mDriverPressure.disconnect();
}

const char* Floor::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Virtual floor counter";
}

void Floor::sdcNewConnection(Sensor::Driver* /*driver*/)
{
    OS::MutexCS cs(mDataMutex);
    mTimestamp = OS::GetTimeMs();
    publishData(mCounters);
}

void Floor::sensorRefresh()
{
    OS::MutexCS cs(mDataMutex);
    if (mDirty) {
        mDirty = false;
        publishData(mCounters);
    }
}

void Floor::onSdlNewData(uint16_t                 handle,
                         const SDK::Sensor::Data* base,
                         uint16_t                 count,
                         uint16_t                 stride)
{
    OS::MutexCS cs(mDataMutex);

    SDK::Sensor::DataBatch batch(base, count, stride);

    if (mDriverStepDetector.matchesDriver(handle)) {
        SDK::SensorDataParser::StepDetector parser(batch[0]);
        mFloorCounter.newStep(parser.getTimestamp());
    } else if (mDriverPressure.matchesDriver(handle)) {
        SDK::SensorDataParser::Pressure parser(batch[0]);
        mFloorCounter.newPressure(parser.getPressure(), parser.getTimestamp());
    }

    Counters counters{};
    counters.up   = mFloorCounter.getFloorsUp();
    counters.down = mFloorCounter.getFloorsDown();

    if (mCounters != counters) {
        LOG_DEBUG("rx up=%u down=%u\n", counters.up, counters.down);
        mCounters  = counters;
        mTimestamp = OS::GetTimeMs();
        mDirty     = true;
        Sensor::Manager::getInstance().wake();
    }
}

void Floor::publishData(const Counters& counters)
{
    LOG_DEBUG("tx up=%u down=%u\n", counters.up, counters.down);
    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(mTimestamp);
    sample.i[SDK::SensorDataParser::FloorCounter::FLOORS_UP]   = static_cast<int32_t>(counters.up);
    sample.i[SDK::SensorDataParser::FloorCounter::FLOORS_DOWN] = static_cast<int32_t>(counters.down);
    mDriver.pushDataSample();
}

} // namespace Sensor
