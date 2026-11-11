/**
 ******************************************************************************
 * @file    ProdTestLcdEeprom.сpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.LcdEeprom"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestLcdEeprom.hpp"
#include "Instances/InstanceI2C.hpp"
#include "N24S64B.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"

namespace ProdTest
{

LcdEeprom::LcdEeprom(const std::string& id) : Interface::IProdTest(id)
{}

bool LcdEeprom::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test LCD EEPROM...\n");

    Hardware::N24S64B eeprom(Instance::I2C::getInstance().getMainBus());

    return ProdTest::Tools::testEEPROM(slog, eeprom);
}

}   // namespace ProdTest
