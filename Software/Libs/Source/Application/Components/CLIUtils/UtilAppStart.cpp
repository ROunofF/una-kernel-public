/**
 *******************************************************************************
 * @file    UtilAppStart.cpp
 * @brief   CLI utility that starts/stops a user application by PID.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilAppStart.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"

#include "Backend.hpp"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Construct a new Components::CliUtils::UtilAppStart object
 */
Components::CliUtils::UtilAppStart::UtilAppStart() :
        UtilBase("appstart"),
        mCommands{
            .execute = UtilAppStart::execute,
            .getName = UtilAppStart::getName,
            .getDesc = UtilAppStart::getDesc,
            .getHelp = UtilAppStart::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to a single instance of the utility
 *
 * @return A reference to the utility object
 */
Components::CliUtils::UtilAppStart& Components::CliUtils::UtilAppStart::getInstance()
{
    static UtilAppStart instance;
    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 *
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t *Components::CliUtils::UtilAppStart::getCommands()
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
const char* Components::CliUtils::UtilAppStart::getName(ICLICommand_t* cmd)
{
    return UtilAppStart::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Utility description
 */
const char* Components::CliUtils::UtilAppStart::getDesc(ICLICommand_t* cmd)
{
    return "starts/stops a user app by PID (dev aid)";
}

/**
 * @brief  Provides a help on how to use the utility
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Empty ""
 */
const char* Components::CliUtils::UtilAppStart::getHelp(ICLICommand_t* cmd)
{
    CLIConsole_print(
        "Start or stop a user app by posting the launcher's own events\n"
        "\n"
        "How to run:\n"
            "start an app - " CLI_ATTR_CMD_HELP "appstart <pid-hex>" CLI_ATTR_DEF "\n"
            "stop an app  - " CLI_ATTR_CMD_HELP "appstart -stop <pid-hex>" CLI_ATTR_DEF "\n"
        "\n"
        "PIDs are the service PIDs from the boot scan (see app_list.json\n"
        "or the kernel log).\n");

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
void Components::CliUtils::UtilAppStart::execute(ICLICommand_t* cmd,
                                                 CLIInput_t*    input,
                                                 int            argc,
                                                 const char**   argv)
{
    (void) cmd;
    (void) input;

    bool stop = (argc == 3) && (strcmp(argv[1], "-stop") == 0);
    const char *pidArg = stop ? argv[2] : ((argc == 2) ? argv[1] : nullptr);

    if (pidArg == nullptr) {
        CLIConsole_print("usage: appstart [-stop] <pid-hex>\n");
        return;
    }

    uint32_t pid = static_cast<uint32_t>(strtoul(pidArg, nullptr, 16));
    if (pid == 0) {
        CLIConsole_print("invalid pid\n");
        return;
    }

    // The same events the launcher GUI posts (Model -> IGuiBackend), so the
    // whole Backend/Manager/AppInstance path below the GUI is exercised.
    IGuiBackend *backend = &Backend::GetInstance();
    bool ok;
    if (stop) {
        ok = backend->sendEventToBackend(G2BEvent::UserAppStop{pid});
    } else {
        // Attempt 0: not a launch the user made from a menu, so a failure
        // has no origin to return to.
        ok = backend->sendEventToBackend(G2BEvent::UserAppStart{pid, 0});
    }

    CLIConsole_print(ok ? "event posted\n" : "failed to post event\n");
}
