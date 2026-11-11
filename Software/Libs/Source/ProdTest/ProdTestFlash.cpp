/**
 ******************************************************************************
 * @file    ProdTestFlash.сpp
 * @brief   Production test for the Flash.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Flash"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestFlash.hpp"
#include "Flash.hpp"
#include "flash_diskio.h"
#include "FileSystem.hpp"

namespace ProdTest
{

Flash::Flash(const std::string& id) : Interface::IProdTest(id)
{}

bool Flash::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Flash (EMMC04G-CT32-01G10)...\n");

    // Initialize EMMC
    ::Flash& flash = ::Flash::GetInstance();

    if (!flash.init()) {
        LOG_ERROR("failed to init the flash\n");
        return false;
    }

    flash.powerModeAlwaysOn();

    uint32_t capacity = flash.blockGetSize() * flash.blockGetCount();

    LOG_INFO("%s\n", slog.print("block size  %u", flash.blockGetSize()));
    LOG_INFO("%s\n", slog.print("block count %u", flash.blockGetCount()));
    LOG_INFO("%s\n", slog.print("capacity %.1f GB (%u)", (static_cast<float>(capacity) / (1 << 30)), capacity));

    return true;
}

}   // namespace ProdTest
