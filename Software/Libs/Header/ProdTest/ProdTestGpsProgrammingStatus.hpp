/**
 ******************************************************************************
 * @file    ProdTestGpsProgrammingStatus.hpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "IUart.hpp"
#include "Gpio.hpp"

namespace ProdTest {

class GpsProgrammingStatus : public Interface::IProdTest
{
public:
    GpsProgrammingStatus(const std::string& id);
    ~GpsProgrammingStatus() override;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    Interface::IUart& mUart;
    Interface::IGpo&  mPwr;
};

}   // namespace ProdTest
