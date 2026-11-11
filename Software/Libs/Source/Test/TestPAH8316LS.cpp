#define LOG_MODULE_PRX      "TestPAH8316LS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestPAH8316LS.hpp"

#include "Gpio.hpp"
#include "System.hpp"
#include "Instances/InstanceI2C.hpp"
#include "OS.hpp"

#include "PAH8316LS.hpp"

#include <cassert>


class BodySensorCallback : public Interface::IBodySensor::Callback {
    void onTouch(bool val) {
        LOG_INFO("%u\n", val);
    }
    void onHeartRate(uint8_t hr, uint8_t trustLevel) {
        LOG_INFO("hr %u, trustLevel %u\n", hr, trustLevel);
    }
};

void TestPAH8316LS_execute()
{
    LOG_INFO("start\n");

    Hardware::PAH8316LS sensor {
            Instance::I2C::getInstance().getBodyBus(),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_INT1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_RST_L),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_MISO)};

    sensor.init();
    sensor.setAccRange(8); // 8G
    sensor.enableDataLogging(false);

    BodySensorCallback cb;
    sensor.registerCallback(&cb);


    while (1) {

        sensor.enable(Hardware::PAH8316LS::OperateMode::TouchOnly);
        OS::Delay(60000);

        sensor.enable(Hardware::PAH8316LS::OperateMode::Auto);
        OS::Delay(60000);

        sensor.disable();

        OS::Delay(10000);
    }

}
