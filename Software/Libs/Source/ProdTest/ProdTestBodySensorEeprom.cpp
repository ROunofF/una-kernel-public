/**
 ******************************************************************************
 * @file    ProdTestBodySensorEeprom.сpp
 * @brief   Production test for the BodySensor EEPROM
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BodySensorEeprom"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestBodySensorEeprom.hpp"
#include "Instances/InstanceI2C.hpp"
#include "N24S64B.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"

namespace ProdTest
{

BodySensorEeprom::BodySensorEeprom(const std::string& id) : Interface::IProdTest(id)
{}

bool BodySensorEeprom::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test EEPROM...\n");

    Hardware::N24S64B eeprom(Instance::I2C::getInstance().getBodyBus());

    return ProdTest::Tools::testEEPROM(slog, eeprom);
}

}   // namespace ProdTest
