/**
 ******************************************************************************
 * @file    UtilModuleInfo.hpp
 * @brief   CLI utility to program/read per-module identification strings.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENTS_CLIUTILS_UTIL_MODULE_INFO_HPP
#define COMPONENTS_CLIUTILS_UTIL_MODULE_INFO_HPP

#include "Porting.h"
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

#include "Components/ModuleInfo/ModuleInfoStorage.hpp"

namespace Components {

namespace CliUtils {

class UtilModuleInfo : public UtilBase
{
public:
    static UtilModuleInfo &getInstance();

    UtilModuleInfo(UtilModuleInfo const&)       = delete;
    void operator=(UtilModuleInfo const&) = delete;

    UtilModuleInfo(UtilModuleInfo&&)            = delete;
    UtilModuleInfo& operator=(UtilModuleInfo&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    using Storage = Component::ModuleInfoStorage;

    ICLICommand_t mCommands;

    explicit UtilModuleInfo();
    ~UtilModuleInfo() override = default;

    static bool isSymbolValid(char s);
    static bool valueCheck(const char* value);

    // Map between a module and its CLI text name ("core"/"view"/...).
    static bool        parseModule(const char* name, Storage::Module& out);
    static const char* moduleName(Storage::Module module);

    // Ensure the file system is mounted (the core module is file-backed). Needed
    // when the command runs before the rest of the system has mounted it.
    static bool mountFS();

    static bool setField(Storage&        storage,
                         Storage::Module module,
                         const char*     flag,
                         const char*     value,
                         bool (Storage::*setter)(Storage::Module, const char*));

    static void printModule(Storage& storage, Storage::Module module);

    // Build a storage instance wired to the watch's I2C buses and core file.
    static Storage makeStorage();

    static const char* getName(ICLICommand_t* cmd);
    static const char* getDesc(ICLICommand_t* cmd);
    static const char* getHelp(ICLICommand_t* cmd);
    static void        execute(ICLICommand_t* cmd,
                               CLIInput_t*    input,
                               int            argc,
                               const char**   argv);
};

} // namespace CliUtils
} // namespace Components

#endif // COMPONENTS_CLIUTILS_UTIL_MODULE_INFO_HPP
