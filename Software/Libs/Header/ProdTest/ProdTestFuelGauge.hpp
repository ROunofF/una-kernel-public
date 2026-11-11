/**
 ******************************************************************************
 * @file    ProdTestFuelGauge.hpp
 * @brief   Production test for the FuelGauge.
 ******************************************************************************
 *
 ******************************************************************************
 */


#include "ProdTest/IProdTest.hpp"
#include "II2c.hpp"

namespace ProdTest {

class FuelGauge : public Interface::IProdTest {
public:
    FuelGauge(const std::string& id);
    ~FuelGauge() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
