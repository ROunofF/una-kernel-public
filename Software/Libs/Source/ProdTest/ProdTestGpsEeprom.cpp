/**
 ******************************************************************************
 * @file    ProdTestGpsEeprom.сpp
 * @brief   Production test for the GPS EEPROM.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.GpsEeprom"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestGpsEeprom.hpp"
#include "Instances/InstanceI2C.hpp"
#include "N24S64B.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"

namespace ProdTest
{

GpsEeprom::GpsEeprom(const std::string& id) : Interface::IProdTest(id)
{}

bool GpsEeprom::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test GPS EEPROM...\n");

    Hardware::N24S64B eeprom(Instance::I2C::getInstance().getSensor2Bus());

    return ProdTest::Tools::testEEPROM(slog, eeprom);
}

}   // namespace ProdTest
