#define LOG_MODULE_PRX      "TestSensorBattery"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorBattery.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorBattery : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorBattery()
        : mSensorBatteryCharging(SDK::Sensor::Type::BATTERY_CHARGING, this)
        , mSensorBatteryLevel(SDK::Sensor::Type::BATTERY_LEVEL, this)
        , mSensorBatteryMetrics(SDK::Sensor::Type::BATTERY_METRICS, this, 2500, 0)
    {
    }

    void run()
    {
        mSensorBatteryCharging.connect();
        mSensorBatteryLevel.connect();
        mSensorBatteryMetrics.connect();

        OS::Delay(60000);

        mSensorBatteryCharging.disconnect();
        mSensorBatteryLevel.disconnect();
        mSensorBatteryMetrics.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorBatteryCharging.matchesDriver(handle)) {
            SDK::SensorDataParser::BatteryCharging p(batch[0]);
            LOG_INFO("connect/charge  : %d/%d\n", static_cast<int>(p.isUsbConnected()), static_cast<int>(p.isCharging()));
        } else if (mSensorBatteryLevel.matchesDriver(handle)) {
            SDK::SensorDataParser::BatteryLevel p(batch[0]);
            LOG_INFO("level           : %7.2f\n", p.getCharge());
        } else if (mSensorBatteryMetrics.matchesDriver(handle)) {
            SDK::SensorDataParser::BatteryMetrics p(batch[0]);
            LOG_INFO("Voltage         : %7.2f\n", p.getVoltage());
            LOG_INFO("Current         : %7.2f\n", p.getCurrent());
            LOG_INFO("AverageCurrent  : %7.2f\n", p.getAverageCurrent());
            LOG_INFO("Capacity        : %7.2f\n", p.getCapacity());
            LOG_INFO("DesignCapacity  : %7.2f\n", p.getDesignCapacity());
        }
    }

private:
    Sensor::Connection mSensorBatteryCharging;
    Sensor::Connection mSensorBatteryLevel;
    Sensor::Connection mSensorBatteryMetrics;
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

void TestSensorBattery_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorBattery test;
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
