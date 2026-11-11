#include "Components/CLIUtils/FS/utilFS.hpp"
#include "Components/CLIUtils/FS/ls.hpp"
#include "Components/CLIUtils/FS/cd.hpp"
#include "Components/CLIUtils/FS/cat.hpp"
#include "Components/CLIUtils/FS/mkdir.hpp"
#include "Components/CLIUtils/FS/rm.hpp"
#include "Components/CLIUtils/FS/md5.hpp"
#include "Components/CLIUtils/FS/stat.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIInput.h"
#include "CLI/CLIParser.h"
#include "FileSystem/Folder.h"
#include "OS.hpp"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

void Components::CliUtils::UtilFS::checkCommand(ICLICommand_t* cmd, CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "ls") == 0) {
        ls_run(argc, argv);
    } else if (strcmp(argv[0], "cd") == 0) {
        cd_run(argc, argv);
    } else if (strcmp(argv[0], "cat") == 0) {
        cat_run(argc, argv, CLIInput_echoEnabled(input));
    } else if (strcmp(argv[0], "mkdir") == 0) {
        mkdir_run(argc, argv);
    } else if (strcmp(argv[0], "rm") == 0) {
        rm_run(argc, argv);
    } else if (strcmp(argv[0], "md5") == 0) {
        md5_run(argc, argv);
    } else if (strcmp(argv[0], "stat") == 0) {
        stat_run(argc, argv);
    } else if (strcmp(argv[0], "help") == 0) {
        getHelp(cmd);
    } else {
        CLIConsole_print("unknown command\n");
    }
}

void Components::CliUtils::UtilFS::prompt(void)
{
    char currDir[FF_MAX_LFN] = { 0 };

    if (f_getcwd(currDir, sizeof(currDir)) != FR_OK) {
        CLIConsole_print("could not get current work directory\n");
    }
    
    CLIConsole_print("%s" CLI_UTIL_PROMPT, currDir);
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Construct a new Components::CliUtils::UtilFS object
 *
 */
Components::CliUtils::UtilFS::UtilFS() :
        UtilBase("fs"),
        mCommands{
            .execute = UtilFS::execute,
            .getName = UtilFS::getName,
            .getDesc = UtilFS::getDesc,
            .getHelp = UtilFS::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to a single instance of the Test CLI
 *         utility
 *
 * @return A reference to the utility object
 */
Components::CliUtils::UtilFS& Components::CliUtils::UtilFS::getInstance()
{
    static UtilFS instance;
    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 *
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t *Components::CliUtils::UtilFS::getCommands()
{
    return &mCommands;
}

/**
 * @brief  Returns the utility name
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Utility name
 */
const char* Components::CliUtils::UtilFS::getName(ICLICommand_t* cmd)
{
    return UtilFS::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Utility description
 */
const char* Components::CliUtils::UtilFS::getDesc(ICLICommand_t* cmd)
{
    return "tool for working with file system";
}

/**
 * @brief  Provides a help on how to use the utility
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Empty ""
 */
const char* Components::CliUtils::UtilFS::getHelp(ICLICommand_t* cmd)
{
    CLIConsole_print("The list of the internal commands:\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "ls" CLI_ATTR_DEF "\n");
    CLIConsole_print("   shows a list of files in the current folder\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "cd" CLI_ATTR_DEF "\n");
    CLIConsole_print("   changes directory\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "rm" CLI_ATTR_DEF "\n");
    CLIConsole_print("   removes a file or a folder\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "cat" CLI_ATTR_DEF "\n");
    CLIConsole_print("   prints file contents to standard output\n");
    CLIConsole_print("   usage:\n");
    CLIConsole_print("     cat <file> [-n] [-page <lines>] [-ofs <offset>]\n");
    CLIConsole_print("     cat <file> -bin [-ascii] [-c <width>] [-n] [-page <lines>] [-ofs <offset>]\n");
    CLIConsole_print("     cat > <file> [-e]\n");
    CLIConsole_print("   options:\n");
    CLIConsole_print("     -n              show line numbers\n");
    CLIConsole_print("     -page <lines>   pause output every <lines> lines\n");
    CLIConsole_print("     -ofs <offset>   start reading from byte offset\n");
    CLIConsole_print("     -bin            show file in binary/hex mode\n");
    CLIConsole_print("     -ascii          show ASCII column in binary mode\n");
    CLIConsole_print("     -c <width>      bytes per line in binary mode\n");
    CLIConsole_print("     -e              echo input while creating a file\n");
    CLIConsole_print("   examples:\n");
    CLIConsole_print("     cat readme.txt\n");
    CLIConsole_print("     cat log.txt -n -page 20\n");
    CLIConsole_print("     cat firmware.bin -bin -ascii -c 16\n");
    CLIConsole_print("     cat firmware.bin -bin -ofs 1024\n");
    CLIConsole_print("     cat > notes.txt -e\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "mkdir" CLI_ATTR_DEF "\n");
    CLIConsole_print("   makes directories\n");
    CLIConsole_print("   mkdir <dirName>\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "stat" CLI_ATTR_DEF "\n");
    CLIConsole_print("   returns total and free spaces in the file system\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "md5" CLI_ATTR_DEF "\n");
    CLIConsole_print("   computes MD5 crc for specified file\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "help" CLI_ATTR_DEF "\n");
    CLIConsole_print("   shows this help\n");
    CLIConsole_print("\n");

    CLIConsole_print(" - " CLI_ATTR_CMD_HELP "q" CLI_ATTR_DEF "\n");
    CLIConsole_print("   quits the utility\n");

    return "";
}

/**
 * @brief  Executes the request to the utility
 *
 * @param  cmd:   Pointer to the command handler functions
 * @param  input: Pointer to the CLI input
 * @param  argc:  Count of the arguments
 * @param  argv:  Pointer to the arguments
 */
void Components::CliUtils::UtilFS::execute(ICLICommand_t* cmd,
                                           CLIInput_t*    input,
                                           int            argc,
                                           const char**   argv)
{
    (void) cmd;
    (void) input;
    (void) argc;
    (void) argv;

    prompt();

    while (true) {
        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "q") == 0) {
                break;
            }

            checkCommand(cmd, input);
            CLIInput_reset(input);
            prompt();
        }

        OS::Delay(5);
    }
}
