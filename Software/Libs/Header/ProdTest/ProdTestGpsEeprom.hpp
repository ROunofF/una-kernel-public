/**
 ******************************************************************************
 * @file    ProdTestGpsEeprom.hpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class GpsEeprom : public Interface::IProdTest {
public:
    GpsEeprom(const std::string& id);
    ~GpsEeprom() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
