#define LOG_MODULE_PRX      "TestDHSensors"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestDHSensors.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestDHSensors : public SDK::Interface::ISensorDataListener
{
public:
    TestDHSensors()
        : mSensorTouch(SDK::Sensor::Type::TOUCH_DETECT, this)
        , mSensorMotion(SDK::Sensor::Type::MOTION_DETECT, this)
        , mSensorStepCounter(SDK::Sensor::Type::STEP_COUNTER, this)
        , mSensorHR(SDK::Sensor::Type::HEART_RATE, this)
    {
    }

    void run()
    {
        mSensorTouch.connect();
        mSensorMotion.connect();
        mSensorStepCounter.connect();

        ::Driver::SwTimer t(2000);
        while (!t.expired()) {
            OS::Delay(100);
        }

        mSensorHR.connect();

        t.start(30000);
        while (!t.expired()) {
            OS::Delay(100);
        }

        mSensorHR.disconnect();

        t.start(30000);
        while (!t.expired()) {
            LOG_INFO("ping\n");
            OS::Delay(1000);
        }

        mSensorTouch.disconnect();
        mSensorMotion.disconnect();
        mSensorStepCounter.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorTouch.matchesDriver(handle)) {
            SDK::SensorDataParser::Touch p(batch[0]);
            LOG_INFO("touch           : %d\n", (int)p.isTouched());
        } else if (mSensorMotion.matchesDriver(handle)) {
            SDK::SensorDataParser::MotionDetect parser(batch[0]);
            LOG_INFO("motion          : %d\n", static_cast<int>(parser.getID()));
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
        }
    }

private:
    Sensor::Connection mSensorTouch;
    Sensor::Connection mSensorMotion;
    Sensor::Connection mSensorStepCounter;
    Sensor::Connection mSensorHR;

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

void TestDHSensors_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestDHSensors test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
