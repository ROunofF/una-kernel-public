#define LOG_MODULE_PRX      "TestSensorAltimeter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorAltimeter.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorAltimeter : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorAltimeter()
        : mSensorT(SDK::Sensor::Type::AMBIENT_TEMPERATURE, this, 1000, 1000)
        , mSensorP(SDK::Sensor::Type::PRESSURE, this, 1000, 1000)
        , mSensorAltimeter(SDK::Sensor::Type::ALTIMETER, this, 1000, 1000)
    {
    }

    void run()
    {
        mSensorT.connect();
        mSensorP.connect();
        mSensorAltimeter.connect();

        ::Driver::SwTimer t(22000);
        for (;;) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        mSensorT.disconnect();
        mSensorP.disconnect();
        mSensorAltimeter.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorT.matchesDriver(handle)) {
            SDK::SensorDataParser::Temperature p(batch[0]);
            LOG_INFO("temperature     : %.2f\n", p.getTemperature());
        } else if (mSensorP.matchesDriver(handle)) {
            SDK::SensorDataParser::Pressure p(batch[0]);
            float altitude = p.getAltitude();
            LOG_INFO("P0 : %.2f, P : %.2f, altitude : %.2f\n", p.getP0(), p.getPressure(), altitude);
        } else if (mSensorAltimeter.matchesDriver(handle)) {
            SDK::SensorDataParser::Altimeter p(batch[0]);
            LOG_INFO("altitude        : %.2f\n", p.getAltitude());
        }
    }

private:
    Sensor::Connection mSensorT;
    Sensor::Connection mSensorP;
    Sensor::Connection mSensorAltimeter;
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

void TestSensorAltimeter_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorAltimeter test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
