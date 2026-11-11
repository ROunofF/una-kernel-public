/**
 ******************************************************************************
 * @file    ProdTestVersion.сpp
 * @brief   Production test for the versions boot, libc, and kernel.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Version"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestVersion.hpp"
#include "System.hpp"

namespace ProdTest
{

Version::Version(const std::string& id) : Interface::IProdTest(id)
{}

bool Version::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Version...\n");

    const char* boot   = Driver::System::GetBootVersionStr() + 3;
    const char* libc   = Driver::System::GetLibcVersionStr() + 3;
    const char* kernel = Driver::System::GetBuildVersionStr();

    LOG_INFO("Bootloader %s\n", boot);
    LOG_INFO("LibC       %s\n", libc);
    LOG_INFO("Kernel     %s\n", kernel);

    slog.print("Bootloader=%s", boot);
    slog.print("LibC=%s",       libc);
    slog.print("Kernel=%s",     kernel);

    return true;
}


}   // namespace ProdTest
