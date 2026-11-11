/**
 ******************************************************************************
 * @file    ProdTestBleName.hpp
 * @brief   Production test to get the BLE Name.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class BleName : public Interface::IProdTest {
public:
    BleName(const std::string& id);
    ~BleName() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
