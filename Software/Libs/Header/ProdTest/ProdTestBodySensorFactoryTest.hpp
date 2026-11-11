/**
 ******************************************************************************
 * @file    ProdTestBodySensor.hpp
 * @brief   Production test for the BodySensor.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "IBodySensor.hpp"
#include "PAH8316LSFactoryTester.hpp"

namespace ProdTest {

class BodySensorFactoryTest : public Interface::IProdTest
{
public:
    BodySensorFactoryTest(const std::string& id);
    ~BodySensorFactoryTest() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    bool run(StringLogger& slog, Hardware::PAH8316LSFactoryTester& ftester);
};

}   // namespace ProdTest
