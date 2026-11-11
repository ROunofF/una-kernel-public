#define LOG_MODULE_PRX      "TestHikingSensors"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestHikingSensors.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestHikingSensors : public SDK::Interface::ISensorDataListener
{
public:
    TestHikingSensors()
        : mSensorGpsLocation(SDK::Sensor::Type::GPS_LOCATION, this)
        , mSensorGpsSpeed(SDK::Sensor::Type::GPS_SPEED, this)
        , mSensorGpsDistance(SDK::Sensor::Type::GPS_DISTANCE, this)
        , mSensorHR(SDK::Sensor::Type::HEART_RATE, this)
        , mSensorStepCounter(SDK::Sensor::Type::STEP_COUNTER, this)
        , mSensorAltimeter(SDK::Sensor::Type::ALTIMETER, this)
        , mSensorFloorCounter(SDK::Sensor::Type::FLOOR_COUNTER, this)
        , mSensorBatteryLevel(SDK::Sensor::Type::BATTERY_LEVEL, this)
    {
    }

    void run()
    {
        mSensorGpsLocation.connect();
        mSensorGpsSpeed.connect();
        mSensorGpsDistance.connect();
        mSensorHR.connect();
        mSensorStepCounter.connect();
        mSensorAltimeter.connect();
        mSensorFloorCounter.connect();
        mSensorBatteryLevel.connect();

        ::Driver::SwTimer t(5000);
        for (uint8_t idx = 0; idx < 300; ++idx) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        mSensorGpsLocation.disconnect();
        mSensorGpsSpeed.disconnect();
        mSensorGpsDistance.disconnect();
        mSensorHR.disconnect();
        mSensorStepCounter.disconnect();
        mSensorAltimeter.disconnect();
        mSensorFloorCounter.disconnect();
        mSensorBatteryLevel.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorBatteryLevel.matchesDriver(handle)) {
            SDK::SensorDataParser::BatteryLevel p(batch[0]);
            LOG_INFO("level           : %7.2f\n", p.getCharge());
        } else if (mSensorGpsLocation.matchesDriver(handle)) {
            SDK::SensorDataParser::GpsLocation parser(batch[0]);
            LOG_INFO("lat             : %f\n",    parser.getLatitude());
            LOG_INFO("lon             : %f\n",    parser.getLongitude());
            LOG_INFO("alt             : %f\n",    parser.getAltitude());
        } else if (mSensorGpsSpeed.matchesDriver(handle)) {
            SDK::SensorDataParser::GpsSpeed parser(batch[0]);
            LOG_INFO("speed           : %7.2f\n", parser.getSpeed());
        } else if (mSensorGpsDistance.matchesDriver(handle)) {
            SDK::SensorDataParser::GpsDistance parser(batch[0]);
            LOG_INFO("distance        : %7.2f\n", parser.getDistance());
        } else if (mSensorHR.matchesDriver(handle)) {
            SDK::SensorDataParser::HeartRate parser(batch[0]);
            if (parser.isDataValid()) {
                LOG_INFO("hr              : %.1f\n", parser.getBpm());
            } else {
                LOG_INFO("hr              : invalid\n");
            }
        } else if (mSensorStepCounter.matchesDriver(handle)) {
            SDK::SensorDataParser::StepCounter p(batch[0]);
            LOG_INFO("step counts     : %ld\n", p.getStepCount());
        } else if (mSensorAltimeter.matchesDriver(handle)) {
            SDK::SensorDataParser::Altimeter p(batch[0]);
            LOG_INFO("altimeter       : %.2f\n", p.getAltitude());
        } else if (mSensorFloorCounter.matchesDriver(handle)) {
            SDK::SensorDataParser::FloorCounter p(batch[0]);
            LOG_INFO("up/down         : %d / %d\n", p.getFloorsUp(), p.getFloorsDown());
        }
    }

private:
    Sensor::Connection mSensorGpsLocation;
    Sensor::Connection mSensorGpsSpeed;
    Sensor::Connection mSensorGpsDistance;
    Sensor::Connection mSensorHR;
    Sensor::Connection mSensorStepCounter;
    Sensor::Connection mSensorAltimeter;
    Sensor::Connection mSensorFloorCounter;
    Sensor::Connection mSensorBatteryLevel;
};

////////////////////////////////////////////////////////////////////////////////
//// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestHikingSensors_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestHikingSensors test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
