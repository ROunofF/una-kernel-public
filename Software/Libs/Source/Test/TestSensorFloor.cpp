#define LOG_MODULE_PRX      "TestSensorFloor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorFloor.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorFloor : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorFloor()
        : mSensorFloorCounter(SDK::Sensor::Type::FLOOR_COUNTER, this)
    {
    }

    void run()
    {
        mSensorFloorCounter.connect();

        OS::Delay(60000);

        mSensorFloorCounter.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorFloorCounter.matchesDriver(handle)) {
            SDK::SensorDataParser::FloorCounter p(batch[0]);
            LOG_INFO("up/down         : %d / %d\n", p.getFloorsUp(), p.getFloorsDown());
        }
    }

private:
    Sensor::Connection mSensorFloorCounter;
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

void TestSensorFloor_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorFloor test;
    OS::Delay(1000);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
