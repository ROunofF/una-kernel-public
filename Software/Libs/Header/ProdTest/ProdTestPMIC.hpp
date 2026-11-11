/**
 ******************************************************************************
 * @file    ProdTestPMIC.hpp
 * @brief   Production test for the PMIC.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class PMIC : public Interface::IProdTest {
public:
    PMIC(const std::string& id);
    ~PMIC() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
