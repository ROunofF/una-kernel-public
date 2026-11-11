/**
 ******************************************************************************
 * @file    ProdTestGpsProgrammingStatus.сpp
 * @brief   Production test for the GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.GpsComm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestGpsProgrammingStatus.hpp"

#include "Instances/InstanceUART.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"

#include "AG3335/AirohaBootCheck.hpp"

#include "OS.hpp"

namespace ProdTest
{

GpsProgrammingStatus::GpsProgrammingStatus(const std::string& id)
    : Interface::IProdTest(id)
    , mUart(Instance::UART::getInstance().getSensor2Bus())
    , mPwr(Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_RST_L))
{
    mPwr.init(Interface::IGpo::Type::PP, false, false);
    mUart.init();
}

GpsProgrammingStatus::~GpsProgrammingStatus()
{
    mUart.deinit();

    mPwr.set(false);
    mPwr.deinit();
}

bool GpsProgrammingStatus::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test communication...\n");

    mUart.startReception();

    Airoha::BootCheck bootCheck(mUart, mPwr);

    bool inBootMode = false;
    bool ok = bootCheck.testComm(inBootMode);

    mUart.stopReception();

    if (ok) {
        if (inBootMode) {
            LOG_INFO("%s\n", slog.print("Not flashed"));
        }
    } else {
        LOG_INFO("%s\n", slog.print("No communication"));
    }

    return ok && !inBootMode;
}

}   // namespace ProdTest
