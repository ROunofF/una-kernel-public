#define LOG_MODULE_PRX      "TestBodySensor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestBodySensor.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestBodySensor : public SDK::Interface::ISensorDataListener
{
public:
    TestBodySensor()
        : mSensorHR(SDK::Sensor::Type::HEART_RATE, this, 2500, 0)
        , mSensorTouch(SDK::Sensor::Type::TOUCH_DETECT, this)
    {
    }

    void runTouch()
    {
        mSensorTouch.connect();

        OS::Delay(60000);

        mSensorTouch.disconnect();
    }

    void runHR()
    {
        mSensorHR.connect();

        OS::Delay(60000);

        mSensorHR.disconnect();
    }

    void runAll()
    {
        mSensorHR.connect();
        mSensorTouch.connect();

        OS::Delay(60000);

        mSensorHR.disconnect();
        mSensorTouch.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorHR.matchesDriver(handle)) {
            SDK::SensorDataParser::HeartRate parser(batch[0]);
            if (parser.isDataValid()) {
                LOG_INFO("hr              : %.1f\n", parser.getBpm());
            } else {
                LOG_INFO("hr              : invalid\n");
            }
        } else if (mSensorTouch.matchesDriver(handle)) {
            SDK::SensorDataParser::Touch parser(batch[0]);
            if (parser.isDataValid()) {
                LOG_INFO("touch           : %d\n", static_cast<int>(parser.isTouched()));
            } else {
                LOG_INFO("touch           : invalid\n");
            }
        }
    }

private:
    Sensor::Connection mSensorHR;
    Sensor::Connection mSensorTouch;
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

void TestBodySensor_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestBodySensor test;
    OS::Delay(1000);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING BODY SENSORS\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        //test.runTouch();
        //test.runHR();
        test.runAll();

        OS::Delay(3000);
}
}
