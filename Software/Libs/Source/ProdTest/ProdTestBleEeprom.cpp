/**
 ******************************************************************************
 * @file    ProdTestBLE.сpp
 * @brief   Production test for the BLE.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BleEeprom"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestBleEeprom.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "Spi.hpp"
#include "N24S64B.hpp"
#include "CoreBlueNRG.hpp"
#include "PeripheralBlueNRG.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"

namespace ProdTest
{

BleEeprom::BleEeprom(const std::string& id) : Interface::IProdTest(id)
{}

bool BleEeprom::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test BLE EEPROM...\n");

    Hardware::N24S64B eeprom(Instance::I2C::getInstance().getSensor1Bus());

    return ProdTest::Tools::testEEPROM(slog, eeprom);
}

}   // namespace ProdTest
