/**
 ******************************************************************************
 * @file    UtilModuleInfo.cpp
 * @brief   CLI utility to program/read per-module identification strings.
 *
 * @details
 *   Command name: MODULE_INFO
 *
 *   Usage:
 *     MODULE_INFO -module <name> [-name <str>] [-revision <str>] [-serial <str>]
 *     MODULE_INFO -module <name>
 *     MODULE_INFO
 *
 *   Options:
 *     -module   <name>  Selects the module (and its storage backend).
 *                       One of: core, view, link1, link2, body.
 *     -name     <str>   Sets the module name.
 *     -revision <str>   Sets the hardware revision.
 *     -serial   <str>   Sets the serial number.
 *
 *   Any subset of -name/-revision/-serial may be given; fields that are not
 *   supplied keep their stored value. With only -module the record is printed;
 *   with no arguments every module is printed.
 *
 *   Examples:
 *     MODULE_INFO -module core -name U5A5-04G -revision 3.1 -serial 12345678
 *     MODULE_INFO -module view -name LS012 -revision 1.3
 *     MODULE_INFO -module body
 *     MODULE_INFO
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Components/CLIUtils/UtilModuleInfo.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"

#include "FileSystem.hpp"
#include "Flash.hpp"
#include "flash_diskio.h"

#include "Instances/InstanceI2C.hpp"
#include "main.h"

#include <string.h>

namespace Components::CliUtils {

UtilModuleInfo::UtilModuleInfo() :
    UtilBase("MODULE_INFO"),
    mCommands{
        .execute = UtilModuleInfo::execute,
        .getName = UtilModuleInfo::getName,
        .getDesc = UtilModuleInfo::getDesc,
        .getHelp = UtilModuleInfo::getHelp,
    }
{}

Components::CliUtils::UtilModuleInfo& UtilModuleInfo::getInstance()
{
    static UtilModuleInfo instance;

    return instance;
}

ICLICommand_t* UtilModuleInfo::getCommands()
{
    return &mCommands;
}

const char* UtilModuleInfo::getName(ICLICommand_t* cmd)
{
    (void) cmd;

    return UtilModuleInfo::getInstance().getUtilName();
}

const char* UtilModuleInfo::getDesc(ICLICommand_t* cmd)
{
    (void) cmd;

    return "program/read name/revision/serial for the watch modules";
}

const char* UtilModuleInfo::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;

    static const char* help =
        "How to run:\n"
        "\n"
            "set fields    - " CLI_ATTR_CMD_HELP "MODULE_INFO -module name [-name str] [-revision str] [-serial str]" CLI_ATTR_DEF "\n"
            "get module    - " CLI_ATTR_CMD_HELP "MODULE_INFO -module name" CLI_ATTR_DEF "\n"
            "get all       - " CLI_ATTR_CMD_HELP "MODULE_INFO" CLI_ATTR_DEF "\n"
        "\n"
        " Available module names:\n"
        "   - " CLI_ATTR_CMD_HELP "core " CLI_ATTR_DEF " : MCU board\n"
        "   - " CLI_ATTR_CMD_HELP "view " CLI_ATTR_DEF " : LCD board\n"
        "   - " CLI_ATTR_CMD_HELP "link1" CLI_ATTR_DEF " : BLE board\n"
        "   - " CLI_ATTR_CMD_HELP "link2" CLI_ATTR_DEF " : GPS board\n"
        "   - " CLI_ATTR_CMD_HELP "body " CLI_ATTR_DEF " : body sensor board\n"
        "\n"
        "Any subset of -name/-revision/-serial may be set at once.\n"
        "\n"
        "Examples\n"
        "\n"
        "     " CLI_ATTR_CMD_HELP "MODULE_INFO -module core -name U5A5-04G -revision 3.1 -serial 12345678" CLI_ATTR_DEF "\n"
        "     " CLI_ATTR_CMD_HELP "MODULE_INFO -module view -name LS012 -revision 1.3" CLI_ATTR_DEF "\n"
        "     " CLI_ATTR_CMD_HELP "MODULE_INFO" CLI_ATTR_DEF "\n";

    return help;
}

/**
 * @brief Check if a single character is allowed in a field string.
 *
 * Allowed: any printable ASCII character, including space (0x20..0x7E).
 */
bool UtilModuleInfo::isSymbolValid(char s)
{
    unsigned char c = (unsigned char) s;

    return c >= 0x20 && c <= 0x7E;
}

bool UtilModuleInfo::valueCheck(const char* value)
{
    while (*value) {
        if (!isSymbolValid(*value)) {
            return false;
        }

        ++value;
    }

    return true;
}

bool UtilModuleInfo::parseModule(const char* name, Storage::Module& out)
{
    struct Entry { const char* name; Storage::Module module; };
    static const Entry table[] = {
        {"core",  Storage::Module::CORE},
        {"view",  Storage::Module::VIEW},
        {"link1", Storage::Module::LINK1},
        {"link2", Storage::Module::LINK2},
        {"body",  Storage::Module::BODY},
    };

    for (const Entry& e : table) {
        if (strcmp(name, e.name) == 0) {
            out = e.module;
            return true;
        }
    }

    return false;
}

const char* UtilModuleInfo::moduleName(Storage::Module module)
{
    switch (module) {
        case Storage::Module::CORE:  return "core";
        case Storage::Module::VIEW:  return "view";
        case Storage::Module::LINK1: return "link1";
        case Storage::Module::LINK2: return "link2";
        case Storage::Module::BODY:  return "body";
        default:                     return "";
    }
}

/**
 * @brief Mount the file system (cached). The core module record is file-backed,
 *        so the command needs the FS up even if it runs before the rest of the
 *        system has mounted it.
 */
bool UtilModuleInfo::mountFS()
{
    static bool mounted;

    if (mounted) {
        return true;
    }

    ::Flash& flash = ::Flash::GetInstance();
    flash_diskio_set_flash(flash);

    if (!flash.init()) {
        CLIConsole_print("Failed to init the flash\n");
        return false;
    }

    flash.powerModeAlwaysOn();

    FS::FileSystem& fs = FS::FileSystem::GetInstance();
    if (!fs.mount()) {
        CLIConsole_print("Failed to mount the file system\n");
        return false;
    }

    mounted = true;

    return true;
}

/**
 * @brief Validate and write one field through the given storage setter.
 * @return true on success, false on a missing/invalid value or storage error.
 */
bool UtilModuleInfo::setField(Storage&        storage,
                              Storage::Module module,
                              const char*     flag,
                              const char*     value,
                              bool (Storage::*setter)(Storage::Module, const char*))
{
    if (!value) {
        CLIConsole_print("failed to get the value for %s\n", flag);
        return false;
    }

    if (strlen(value) > Storage::maxFieldLen()) {
        CLIConsole_print("value for %s is too long\n", flag);
        return false;
    }

    if (!valueCheck(value)) {
        CLIConsole_print("value for %s is invalid\n", flag);
        return false;
    }

    if (!(storage.*setter)(module, value)) {
        CLIConsole_print("failed to write %s\n", flag);
        return false;
    }

    return true;
}

/**
 * @brief Build a storage instance wired to the watch's I2C buses and core file.
 */
UtilModuleInfo::Storage UtilModuleInfo::makeStorage()
{
    Instance::I2C& i2c = Instance::I2C::getInstance();

    return Storage(i2c.getMainBus(),      // view
                   i2c.getSensor1Bus(),   // link1
                   i2c.getSensor2Bus(),   // link2
                   i2c.getBodyBus(),      // body
                   MODULE_INFO_CORE_FILE_FULL_PATH);
}

/**
 * @brief Read a module record and print it.
 */
void UtilModuleInfo::printModule(Storage& storage, Storage::Module module)
{
    Storage::Info info{};

    if (!storage.read(module, info)) {
        CLIConsole_print("%-6s : read error\n", moduleName(module));
        return;
    }

    CLIConsole_print("%-6s : name=%s revision=%s serial=%s\n",
            moduleName(module),
            info.name, info.revision, info.serial);
}

void UtilModuleInfo::execute(ICLICommand_t* cmd,
                             CLIInput_t*    input,
                             int            argc,
                             const char**   argv)
{
    (void) cmd;
    (void) input;

    // The core module record lives on the file system; make sure it is mounted
    // before talking to the storage layer (which no longer mounts it itself).
    if (!mountFS()) {
        return;
    }

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    Storage storage = makeStorage();

    const char* module = CLIParser_getValueAsString(&p, "-module");

    if (CLIParser_check(&p, "-module") && !module) {
        CLIConsole_print("failed to get the module\n");
        return;
    }

    if (!module) {
        const Storage::Module all[] = {
            Storage::Module::CORE,
            Storage::Module::VIEW,
            Storage::Module::LINK1,
            Storage::Module::LINK2,
            Storage::Module::BODY,
        };

        for (uint8_t idx = 0; idx < ARRAY_SIZE(all); ++idx) {
            printModule(storage, all[idx]);
        }

        return;
    }

    Storage::Module m;
    if (!parseModule(module, m)) {
        CLIConsole_print("unknown module '%s'\n", module);
        return;
    }

    bool anySet = false;

    if (CLIParser_check(&p, "-name")) {
        if (!setField(storage, m, "-name", CLIParser_getValueAsString(&p, "-name"),
                      &Storage::setName)) {
            return;
        }
        anySet = true;
    }

    if (CLIParser_check(&p, "-revision")) {
        if (!setField(storage, m, "-revision", CLIParser_getValueAsString(&p, "-revision"),
                      &Storage::setRevision)) {
            return;
        }
        anySet = true;
    }

    if (CLIParser_check(&p, "-serial")) {
        if (!setField(storage, m, "-serial", CLIParser_getValueAsString(&p, "-serial"),
                      &Storage::setSerial)) {
            return;
        }
        anySet = true;
    }

    if (!anySet) {
        printModule(storage, m);
    }
}

} // namespace Components::CliUtils
