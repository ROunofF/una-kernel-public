#define LOG_MODULE_PRX      "TestHRMonSensors"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestHRMonSensors.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
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
        : mSensorHR(SDK::Sensor::Type::HEART_RATE, this)
    {
    }

    void run()
    {
        mSensorHR.connect();

        ::Driver::SwTimer t(30000);
        for (uint8_t idx = 0; idx < 300; ++idx) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        mSensorHR.disconnect();
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
        }
    }

private:
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

void TestHRMonSensors_execute()
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
