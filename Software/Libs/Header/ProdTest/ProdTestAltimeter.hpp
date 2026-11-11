/**
 ******************************************************************************
 * @file    ProdTestAltimeter.hpp
 * @brief   Production test for the Altimeter.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "MS5837.hpp"

namespace ProdTest {

class Altimeter : public Interface::IProdTest {
public:
    Altimeter(const std::string& id);
    ~Altimeter() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    bool run(StringLogger& slog, Hardware::MS5837& altimeter);
};

}   // namespace ProdTest
