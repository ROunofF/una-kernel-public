/**
 ******************************************************************************
 * @file    ProdTestIMU.hpp
 * @brief   Production test for the IMU.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "Hardware/BMI270/BMI270.hpp"

namespace ProdTest {

class Imu : public Interface::IProdTest {
public:
    Imu(const std::string& id);
    ~Imu() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    bool run(StringLogger& slog, Hardware::BMI270& imu);

    bool testLoopback(Hardware::BMI270& imu);
};

}   // namespace ProdTest
