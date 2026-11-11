/**
 ******************************************************************************
 * @file    ProdTestButtons.hpp
 * @brief   Production test for the Buttons.
 ******************************************************************************
 *
 ******************************************************************************
 */


#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class Buttons : public Interface::IProdTest {
public:
    Buttons(const std::string& id);

    ~Buttons() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
