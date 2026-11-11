/**
 ******************************************************************************
 * @file    ProdTestGps.сpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Gps"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestGPS.hpp"
#include "ProdTest/GPS/TestGPS.hpp"
#include "Instances/InstanceUART.hpp"
#include "Gpio.hpp"
#include "OS.hpp"
#include "SwTimer.hpp"
#include "CLI/CLIConsole.h"

namespace ProdTest
{

Gps::Gps(const std::string& id)
    : Interface::IProdTest(id)
{
}

Gps::~Gps()
{}

bool Gps::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test GPS (AG3335M)...\n");

    Interface::IUart& mUart(Instance::UART::getInstance().getSensor2Bus());
    Interface::IGpo&  mPwr(Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_RST_L));

    ProdTest::TestGPS test(mUart, mPwr);

    return test.run(slog, input);
}

}   // namespace ProdTest
