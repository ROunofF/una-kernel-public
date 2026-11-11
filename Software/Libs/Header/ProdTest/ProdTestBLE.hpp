/**
 ******************************************************************************
 * @file    ProdTestBLE.hpp
 * @brief   Production test for the BLE.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "CoreBlueNRG.hpp"
#include "PeripheralBlueNRG.hpp"
#include "CLI/CLIInput.h"

namespace ProdTest {

class Ble : public Interface::IProdTest {
public:
    Ble(const std::string& id);
    ~Ble() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    bool run(StringLogger& slog, ::Ble::CoreBlueNRG& core, CLIInput_t* input, bool unpair);
    bool run(StringLogger& slog, ::Ble::PeripheralBlueNRG& ble, CLIInput_t* input, bool unpair);
};

}   // namespace ProdTest
