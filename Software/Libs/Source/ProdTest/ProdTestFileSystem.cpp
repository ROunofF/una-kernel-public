/**
 ******************************************************************************
 * @file    ProdTestFileSystem.сpp
 * @brief   Production test for the Flash.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.FileSystem"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestFileSystem.hpp"
#include "Flash.hpp"
#include "flash_diskio.h"
#include "FileSystem.hpp"

namespace ProdTest
{

FileSystem::FileSystem(const std::string& id) : Interface::IProdTest(id)
{}

bool FileSystem::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test File System...\n");

    // Initialize EMMC
    ::Flash &flash = ::Flash::GetInstance();
    flash_diskio_set_flash(flash);

    if (!flash.init()) {
        return false;
    }

    flash.powerModeAlwaysOn();

    OS::Delay(TEST_DELAY);

    // Initialize File System
    FS::FileSystem &fs = FS::FileSystem::GetInstance();

    if (!fs.mount()) {
        LOG_INFO("%s\n", slog.print("File system not ready. Factory reset in progress..."));
        if (!fs.factoryReset()) {
            LOG_ERROR("%s\n", slog.print("Failed to make factory reset"));
            return false;
        }
    }

    return true;
}

}   // namespace ProdTest
