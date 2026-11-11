/**
 ******************************************************************************
 * @file    ProdTestTools.сpp
 * @brief   Production test tool set
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Tools"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/Core/ProdTestTools.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"

namespace ProdTest
{

bool Tools::checkAddress(Interface::II2c &i2c, uint8_t devAddr)
{
    Driver::SwTimer t(SW_TIMER_SECONDS(1));

    while (!t.expired()) {
        if (i2c.isDeviceReady(devAddr)) {
            return true;
        }

        OS::Delay(20);
    }

    return false;
}

bool Tools::testEEPROM(StringLogger& slog, Hardware::N24S64B& eeprom)
{
    if (!eeprom.isReady()) {
        LOG_ERROR("%s\n", slog.print("I2C address not detected"));
        return false;
    }

    Hardware::N24S64B::UniqueId_t uniqueId {};
    if (!eeprom.readUniqueId(uniqueId)) {
        LOG_ERROR("%s\n", slog.print("No EEPROM found"));
        return false;
    }

    char  strID[Hardware::N24S64B::skUniqueIdSize * 3];
    char* octet = strID;
    for (uint8_t idx = 0; idx < Hardware::N24S64B::skUniqueIdSize; ++idx) {
        sprintf(octet, "%02X", uniqueId.bytes[idx]);

        octet += 2;

        if (idx < (Hardware::N24S64B::skUniqueIdSize - 1)) {
            *octet = ' ';
        } else {
            *octet = '\0';
        }

        octet += 1;
    }

    LOG_DEBUG("%s\n", slog.print("EEPROM ID: %s", strID));

    return true;
}

}   // namespace ProdTest
