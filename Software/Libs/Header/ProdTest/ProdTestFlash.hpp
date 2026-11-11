/**
 ******************************************************************************
 * @file    ProdTestFlash.hpp
 * @brief   Production test for the Flash.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class Flash : public Interface::IProdTest {
public:
    Flash(const std::string& id);
    ~Flash() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
