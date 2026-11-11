/**
 *******************************************************************************
 * @file    UtilReset.cpp
 * @brief   CLI utility that allows you to reset the device
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilReset.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"

/**
 * @brief Construct a new Components::CliUtils::UtilReset object
 * 
 */
Components::CliUtils::UtilReset::UtilReset() :
        UtilBase("reset"),
        mCommands{
            .execute = UtilReset::execute,
            .getName = UtilReset::getName,
            .getDesc = UtilReset::getDesc,
            .getHelp = UtilReset::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to a single instance of the Test CLI
 *         utility
 * 
 * @return A reference to the utility object
 */
Components::CliUtils::UtilReset& Components::CliUtils::UtilReset::getInstance()
{
    static UtilReset instance;
    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 * 
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t *Components::CliUtils::UtilReset::getCommands()
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
const char* Components::CliUtils::UtilReset::getName(ICLICommand_t* cmd)
{
    return UtilReset::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Utility description
 */
const char* Components::CliUtils::UtilReset::getDesc(ICLICommand_t* cmd)
{
    return "reboots the system";
}

/**
 * @brief  Provides a help on how to use the utility
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Empty ""
 */
const char* Components::CliUtils::UtilReset::getHelp(ICLICommand_t* cmd)
{
    CLIConsole_print("The 'reset' utility can be used to rebooting the system\n");

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
void Components::CliUtils::UtilReset::execute(ICLICommand_t* cmd,
                                              CLIInput_t*    input,
                                              int            argc,
                                              const char**   argv)
{
    (void) cmd;
    (void) input;
    (void) argc;
    (void) argv;

    CLIConsole_print("system reset\n");

    WAIT(100);

    NVIC_SystemReset();
}
