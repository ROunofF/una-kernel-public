/**
 ******************************************************************************
 * @file    ProdTestPMIC.сpp
 * @brief   Production test for the PMIC.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.PMIC"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestPMIC.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"
#include "Pmic.hpp"
#include "Gpio.hpp"
#include "Instances/InstanceI2C.hpp"
#include "SwTimer.hpp"

namespace ProdTest
{

PMIC::PMIC(const std::string& id) : Interface::IProdTest(id)
{}

bool PMIC::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test PMIC (PCA9420)...\n");

    Interface::II2c &i2c = Instance::I2C::getInstance().getMainBus();

    //// Check address on the I2C bus
    if (!ProdTest::Tools::checkAddress(i2c, Hardware::PCA9420::skI2cAddr)) {
        LOG_ERROR("%s\n", slog.print("I2C address 0x%02X not detected", Hardware::PCA9420::skI2cAddr));
        return false;
    }

    //// Start PMIC
    ::Pmic &pmic = ::Pmic::GetInstance();
    pmic.config(i2c, Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_INT_L));

    if (!pmic.init()) {
        LOG_INFO("%s\n", slog.print("failed to init the PMIC"));
        return false;
    }

    bool usbDetected     = pmic.isUsbDetected();
    bool batteryDetected = pmic.isBatteryDetected();
    bool batteryCharging = pmic.isCharging();

    //// Battery detection sequence is not automatically triggered
    //// when PMIC is powered only from battery (VBAT).
    if (usbDetected) {
        LOG_INFO("%s\n", slog.print("Running from USB"));
    }

    if (!usbDetected || batteryDetected) {
        LOG_INFO("%s\n", slog.print("Battery detected. %s", (usbDetected & batteryCharging) ? "charging" : ""));
    } else if (!batteryDetected) {
        LOG_WARNING("%s\n", slog.print("No battery detected!"));
    }

    return true;
}

}   // namespace ProdTest
