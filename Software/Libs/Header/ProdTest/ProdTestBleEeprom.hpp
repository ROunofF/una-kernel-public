/**
 ******************************************************************************
 * @file    ProdTestBLE.hpp
 * @brief   Production test for the BLE EEPROM.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class BleEeprom : public Interface::IProdTest {
public:
    BleEeprom(const std::string& id);
    ~BleEeprom() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
