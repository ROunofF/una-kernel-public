#define LOG_MODULE_PRX      "TestSensorPressure"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorPressure.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorPressure : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorPressure(uint16_t interval)
        : mSensorPressure(SDK::Sensor::Type::PRESSURE, this)
        , mInterval(interval)
    {
    }

    void start()
    {
        mSensorPressure.connect(mInterval);
    }

    void stop()
    {
        mSensorPressure.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorPressure.matchesDriver(handle)) {
            SDK::SensorDataParser::Pressure p(batch[0]);
            float altitude = p.getAltitude();
            LOG_INFO("I:%5u pressure        : %.2f, altitude        : %.2f\n", mInterval, p.getPressure(), altitude);
        }
    }

private:
    Sensor::Connection mSensorPressure;
    const uint16_t     mInterval;
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

void TestSensorPressure_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorPressure test1(200);
    TestSensorPressure test2(1000);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test1.start();
        test2.start();

        ::Driver::SwTimer t(15000);
        for (;;) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        test1.stop();
        test2.stop();

        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
