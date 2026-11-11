/**
 ******************************************************************************
 * @file    ProdTestVersion.hpp
 * @brief   Production test for the versions boot, libc, and kernel.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */


#include "ProdTest/IProdTest.hpp"

namespace ProdTest {

class Version : public Interface::IProdTest {
public:
    Version(const std::string& id);

    ~Version() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
