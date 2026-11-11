/**
 ******************************************************************************
 * @file    ProdTestPMIC.hpp
 * @brief   Production test for the PMIC.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "II2c.hpp"
#include "ProdTest/Core/StringLogger.hpp"
#include "Hardware/N24S64B.hpp"

namespace ProdTest {

class Tools {
public:
    static bool checkAddress(Interface::II2c &i2c, uint8_t devAddr);
    static bool testEEPROM(StringLogger& slog, Hardware::N24S64B& eeprom);
};

}   // namespace ProdTest
