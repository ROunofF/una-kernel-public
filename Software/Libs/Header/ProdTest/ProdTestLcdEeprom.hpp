/**
 ******************************************************************************
 * @file    ProdTestLcdEeprom.hpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class LcdEeprom : public Interface::IProdTest {
public:
    LcdEeprom(const std::string& id);
    ~LcdEeprom() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
