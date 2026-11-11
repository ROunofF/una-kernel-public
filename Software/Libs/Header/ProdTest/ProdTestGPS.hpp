/**
 ******************************************************************************
 * @file    ProdTestGPS.hpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "CLI/CLIInput.h"
#include <string_view>
#include <string>
#include <vector>
#include <optional>

#include "AG3335/AirohaAG3335M.hpp"

namespace ProdTest {

class Gps : public Interface::IProdTest
{
public:
    Gps(const std::string& id);
    ~Gps() override;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;
};

}   // namespace ProdTest
