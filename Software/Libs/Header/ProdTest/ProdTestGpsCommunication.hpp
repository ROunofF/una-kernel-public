/**
 ******************************************************************************
 * @file    ProdTestGpsCommunication.hpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "IUart.hpp"
#include "Gpio.hpp"

namespace ProdTest {

class GpsCommunication : public Interface::IProdTest
{
public:
    GpsCommunication(const std::string& id);
    ~GpsCommunication() override;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    Interface::IUart& mUart;
    Interface::IGpo&  mPwr;
};

}   // namespace ProdTest
