/**
 ******************************************************************************
 * @file    ProdTestAltimeter.сpp
 * @brief   Production test for the Altimeter.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Altimeter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestAltimeter.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "OS.hpp"

namespace ProdTest
{

Altimeter::Altimeter(const std::string& id) : Interface::IProdTest(id)
{}

bool Altimeter::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Altimeter (MS5837)...\n");

    ////////////////////////////////
    //// Power on
    ////////////////////////////////

    Interface::IGpo& enablePin = Driver::Gpio::GetInstance(Driver::Gpio::Pin::ALTIMETER_EN);
    enablePin.init(Interface::IGpo::Type::PP, true, false);
    OS::Delay(10);

    ////////////////////////////////
    //// Init and read data
    ////////////////////////////////

    Hardware::MS5837 altimeter( Instance::I2C::getInstance().getBodyBus() );

    bool ok = run(slog, altimeter);

    ////////////////////////////////
    //// Power off
    ////////////////////////////////

    enablePin.set(false);

    return ok;

}

bool Altimeter::run(StringLogger& slog, Hardware::MS5837& altimeter)
{
    if (!altimeter.isReady()) {
        LOG_ERROR("%s\n", slog.print("I2C address not detected"));
        return false;
    }

    if (!altimeter.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init"));
        return false;
    }

    float t;
    float p;
    if (!altimeter.get(t, p)) {
        LOG_ERROR("%s\n", slog.print("Failed to get get data"));
        return false;
    }

    LOG_INFO("%s\n", slog.print("Temperature: %f", t));
    LOG_INFO("%s\n", slog.print("Pressure:    %f", p));

    return true;
}

}   // namespace ProdTest
