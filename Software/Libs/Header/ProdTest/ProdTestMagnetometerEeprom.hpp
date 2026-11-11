/**
 ******************************************************************************
 * @file    ProdTestMagnetometerEeprom.hpp
 * @brief   Production test for the Magnetometer Eeprom.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
//#include "Hardware/BMM350.hpp"

namespace ProdTest {

class MagnetometerEeprom : public Interface::IProdTest {
public:
    MagnetometerEeprom(const std::string& id);
    ~MagnetometerEeprom() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
