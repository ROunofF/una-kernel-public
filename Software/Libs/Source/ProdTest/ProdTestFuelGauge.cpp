/**
 ******************************************************************************
 * @file    ProdTestFuelGauge.сpp
 * @brief   Production test for the FuelGauge.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.FuelGauge"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestFuelGauge.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"
#include "FuelGauge.hpp"
#include "Gpio.hpp"
#include "Instances/InstanceI2C.hpp"
#include "SwTimer.hpp"

namespace ProdTest
{

FuelGauge::FuelGauge(const std::string& id) : Interface::IProdTest(id)
{}

bool FuelGauge::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test FuelGauge (MAX17262)...\n");

    Interface::II2c &i2c = Instance::I2C::getInstance().getMainBus();

    //// Check address on the I2C bus
    if (!ProdTest::Tools::checkAddress(i2c, Hardware::MAX17262::skI2cAddr)) {
        LOG_ERROR("%s\n", slog.print("FuelGauge (MAX17262) I2C address 0x%02X not detected", Hardware::MAX17262::skI2cAddr));
        return false;
    }

    //// Start FuelGauge
    ::FuelGauge &fuelGauge = ::FuelGauge::GetInstance();
    fuelGauge.config(i2c);

    if (!fuelGauge.init()) {
        LOG_INFO("%s\n", slog.print("failed to init the fuel gauge"));
        return false;
    }

    float bateryVoltage   = fuelGauge.getBatteryVoltage();
    float batteryLevel    = fuelGauge.getBatteryLevel();
    float batteryRawLevel = fuelGauge.getRawBatteryLevel();

    LOG_INFO("%s\n", slog.print("Battery: %.3fV, %.3f%% (real level %.3f%%)", bateryVoltage,
                                                                              batteryLevel,
                                                                              batteryRawLevel));

    return true;
}

}   // namespace ProdTest
