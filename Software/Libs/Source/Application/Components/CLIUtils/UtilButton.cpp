/**
 *******************************************************************************
 * @file    UtilButton.cpp
 * @brief   CLI utility for buttons testing
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilButton.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "OS.hpp"
#include "Wdg.hpp"
#include "HWButtons.hpp"

#include <string.h>

class ButtonAgent : public Interface::IButtons::Callback
{
public:
    explicit ButtonAgent() : mButtonEvents()
    {
        mButtonEvents.init("ButtonAgent");
    }

    virtual ~ButtonAgent() {}

    void refresh ()
    {
        if (mButtonEvents.empty()) {
            return;
        }

        Interface::IButtons::Id id;
        mButtonEvents.pop(id);

        switch (id) {
            case Interface::IButtons::Id::SW1: CLIConsole_print("SW1\n");  break;
            case Interface::IButtons::Id::SW2: CLIConsole_print("SW2\n");  break;
            case Interface::IButtons::Id::SW3: CLIConsole_print("SW3\n");  break;
            case Interface::IButtons::Id::SW4: CLIConsole_print("SW4\n");  break;
        default: CLIConsole_print("unknown event\n");
        }
    }

private:
    void onClick(Interface::IButtons::Id id) override
    {
        mButtonEvents.push(id);
    }

    OS::Queue<Interface::IButtons::Id, 5> mButtonEvents;
};

/**
 * @brief Construct a new Components::CliUtils::UtilButton object
 * 
 */
Components::CliUtils::UtilButton::UtilButton() :
    UtilBase("button"),
    mCommands{
        .execute = UtilButton::execute,
        .getName = UtilButton::getName,
        .getDesc = UtilButton::getDesc,
        .getHelp = UtilButton::getHelp,
    }
{
}

/**
 * @brief  Returns the reference to a single instance of the Test CLI
 *         utility
 * 
 * @return A reference to the utility object
 */
Components::CliUtils::UtilButton& Components::CliUtils::UtilButton::getInstance()
{
    static UtilButton instance;

    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 * 
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t* Components::CliUtils::UtilButton::getCommands()
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
const char* Components::CliUtils::UtilButton::getName(ICLICommand_t* cmd)
{
    return UtilButton::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Utility description
 */
const char *Components::CliUtils::UtilButton::getDesc(ICLICommand_t* cmd)
{
    (void) cmd;

    return "tests the buttons";
}

/**
 * @brief  Provides a help on how to use the utility
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Empty ""
 */
const char* Components::CliUtils::UtilButton::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;

    CLIConsole_print("The 'button' utility can be used for testing the button\n");

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
void Components::CliUtils::UtilButton::execute(ICLICommand_t* cmd,
                                               CLIInput_t*    input,
                                               int            argc,
                                               const char**   argv)
{
    (void) cmd;
    (void) input;
    (void) argc;
    (void) argv;

    CLIConsole_print("press ESC to quit\n");

    ButtonAgent btnAgent;
    HWButtons::GetInstance().attachCallback(&btnAgent);

    while (true) {
        Driver::Wdg::GetInstance().update();

        if (CLIConsole_kbhit() == true) {
            if (CLIConsole_getchar() == CLI_ASCII_ESC) {
                CLIConsole_print("quit\n");
                break;
            }
        }

        btnAgent.refresh();

        WAIT(10);
    }

    HWButtons::GetInstance().detachCallback(&btnAgent);
}
