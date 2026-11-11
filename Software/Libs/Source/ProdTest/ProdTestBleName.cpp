/**
 ******************************************************************************
 * @file    ProdTestBleName.сpp
 * @brief   Production test to get the BLE Name.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BleName"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestBleName.hpp"
#include "System.hpp"

namespace ProdTest
{

BleName::BleName(const std::string& id) : Interface::IProdTest(id)
{}

bool BleName::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test BLE NAME...\n");

    char fullBleName[32] {};
    snprintf(fullBleName, sizeof(fullBleName), "%s %s",
            Driver::System::GetModelNameStr(),
            Driver::System::GetDeviceIdStr());

    LOG_INFO("%s\n", slog.print("%s", fullBleName));

    return true;
}

}   // namespace ProdTest
