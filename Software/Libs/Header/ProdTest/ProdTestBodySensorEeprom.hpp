/**
 ******************************************************************************
 * @file    ProdTestBodySensorEeprom.hpp
 * @brief   Production test for the Body Sensor EEPROM.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class BodySensorEeprom : public Interface::IProdTest {
public:
    BodySensorEeprom(const std::string& id);
    ~BodySensorEeprom() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
