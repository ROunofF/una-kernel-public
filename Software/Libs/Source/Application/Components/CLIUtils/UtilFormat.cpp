/**
 *******************************************************************************
 * @file    UtilFormat.cpp
 * @brief   CLI utility that allows you to format the file system
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilFormat.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "Flash.hpp"
#include "flash_diskio.h"
#include "FileSystem/FileSystem.hpp"

namespace Components::CliUtils {

/**
 * @brief Constructor
 */
UtilFormat::UtilFormat() :
        UtilBase("format"),
        mCommands{
            .execute = UtilFormat::execute,
            .getName = UtilFormat::getName,
            .getDesc = UtilFormat::getDesc,
            .getHelp = UtilFormat::getHelp,
        }
{
}

/**
 * @brief Get singleton instance of the Format CLI utility.
 * @return Reference to the utility object.
 */
UtilFormat& UtilFormat::getInstance()
{
    static UtilFormat instance;
    return instance;
}


/**
 * @brief Get the table with pointers to the command handlers.
 * @return Pointer to ICLICommand_t.
 */
ICLICommand_t *UtilFormat::getCommands()
{
    return &mCommands;
}

/**
 * @brief Get the utility name
 * @param cmd Unused.
 * @return Utility name string.
 */
const char* UtilFormat::getName(ICLICommand_t* cmd)
{
    return UtilFormat::getInstance().getUtilName();
}

/**
 * @brief Get a short description of the utility.
 * @param cmd Unused.
 * @return Brief description string.
 */
const char* UtilFormat::getDesc(ICLICommand_t* cmd)
{
    return "format the file system";
}

/**
 * @brief Print help/usage for the utility.
 * @param cmd Unused.
 * @return NUL-terminated help text (owned by the function).
 */
const char* UtilFormat::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;

    static const char* help =
        CLI_ATTR_CMD_HELP "format <-d drive>" CLI_ATTR_DEF "\n"
        "   format the specified drive\n"
        "\n"
        " Available drive values:\n"
        "   - " CLI_ATTR_CMD_HELP "0" CLI_ATTR_DEF " : UNA BOOT drive\n"
        "   - " CLI_ATTR_CMD_HELP "1" CLI_ATTR_DEF " : UNA SYS drive\n"
        "   - " CLI_ATTR_CMD_HELP "2" CLI_ATTR_DEF " : UNA WATCH drive\n"
        ;

    return help;
}

/**
 * @brief Ask user for confirmation before formatting.
 *
 * Prints "are you sure? (y/n)" and waits for input.
 * Timeout is 10 seconds. Returns true only if user presses 'y'.
 *
 * @return true if confirmed, false otherwise.
 */
bool UtilFormat::waitConfirm(void)
{
    CLIConsole_print("are you sure? (y/n): ");

    TimerSysMs_t t;
    TimerSysMs_startWithValue(&t, 10000);
    while (true) {
        if (TimerSysMs_check(&t)) {
            CLIConsole_print("\n");
            return false;
        }

        if (CLIConsole_kbhit()) {
            CLIConsole_print("\n");
            break;
        }

        OS::Delay(10);
    }

    if (CLIConsole_getchar() != 'y') {
        return false;
    }

    return true;
}

/**
 * @brief Execute the Format utility: parse arguments, validate input,
 *        and perform formatting of the selected disk.
 *
 * @param cmd   Unused.
 * @param input Unused (console input context).
 * @param argc  Argument count (command name + optional switches).
 * @param argv  Argument values.
 *
 * Supported options:
 *   - "-d <disk_number>" : required, selects disk (0,1,2)
 *   - "-force"           : optional, skips confirmation prompt
 */
void UtilFormat::execute(ICLICommand_t* cmd,
                         CLIInput_t*    input,
                         int            argc,
                         const char**   argv)
{
    (void) cmd;
    (void) input;
    (void) argc;
    (void) argv;

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    if (!CLIParser_check(&p, "-d")) {
        CLIConsole_print("unspecified the disk\n");
        return;
    }

    uint32_t disk;
    if (!CLIParser_getValueAsUI32(&p, "-d", &disk) || disk > 2) {
        CLIConsole_print("invalid parameter value\n");
        return;
    }

    if (!CLIParser_check(&p, "-force")) {
        if (!waitConfirm()) {
            return;
        }
    }

    FS::FileSystem& fs = FS::FileSystem::GetInstance();

    if (!fs.format(disk)) {
        CLIConsole_print("disk %ld is not formated\n", disk);
        return;
    }

    CLIConsole_print("OK\n");
}

}
