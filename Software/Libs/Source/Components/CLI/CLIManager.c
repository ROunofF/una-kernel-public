/**
 ******************************************************************************
 * @file    CLIManager.c
 * @brief   This component links together all the components of the console
 *          and manages their work. Also, it can add to the console memory
 *          leak detector and indicate about memory leaks.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CLI/CLIManager.h"
#include "CLI/CLIConsole.h"
#include "TimerSysMs.h"

#if CLI_CHECK_MEMORY_LEAK == 1
#include "Components/Heap/HeapMgr.h"
#include "Components/Heap/HeapMap.h"
#endif

#include <string.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static void CLIManager_displayCommand(CLIManager_t* mgr,
                                      const char*   cmdName,
                                      const char*   cmdDesc,
                                      uint32_t      adjustTo)
{
    CLIConsole_print("%s%s%s", mgr->colorMap.attrCmdHelp, cmdName, mgr->colorMap.attrDef);

    for (uint32_t idx = strlen(cmdName); idx < adjustTo; ++idx) {
        CLIConsole_print(" ");
    }

    CLIConsole_print("- %s\n", cmdDesc);
}

static void CLIManager_displayMenu(CLIManager_t* mgr)
{
    if (CLIInput_getArgC(&mgr->input) != 1) {
        CLIConsole_print("error parameter list\n");
        return;
    }

    uint32_t maxCmdLength = 4;

    for (uint8_t idx = 0; idx < CLIRegistry_getCount(&mgr->registry); ++idx) {
        ICLICommand_t* cmd = CLIRegistry_getItem(&mgr->registry, idx);
        uint32_t cmdLength = strlen(cmd->getName(cmd));

        if (maxCmdLength < cmdLength) {
            maxCmdLength = cmdLength;
        }
    }

    maxCmdLength += 1;

    CLIManager_displayCommand(mgr, "help", "shows command's help. An example: 'help ping'", maxCmdLength);

    for (uint8_t idx = 0; idx < CLIRegistry_getCount(&mgr->registry); ++idx) {
        ICLICommand_t* cmd = CLIRegistry_getItem(&mgr->registry, idx);
        CLIManager_displayCommand(mgr, cmd->getName(cmd), cmd->getDesc(cmd), maxCmdLength);
    }
}

static void CLIManager_displayCommandHelp(ICLICommand_t* cmd)
{
    CLIConsole_print("%s\n", cmd->getDesc(cmd));
    CLIConsole_print("\n");
    CLIConsole_print("%s\n", cmd->getHelp(cmd));
}

static void CLIManager_displayHelp(CLIManager_t* mgr)
{
    if (CLIInput_getArgC(&mgr->input) != 2) {
        CLIConsole_print("error parameter list\n");
        return;
    }

    ICLICommand_t* cmd = CLIRegistry_getCommand(&mgr->registry, CLIInput_getArgV(&mgr->input)[1]);

    if (cmd != NULL) {
        CLIManager_displayCommandHelp(cmd);
    } else {
        cmd = CLIRegistry_getCommand(&mgr->registryHidden, CLIInput_getArgV(&mgr->input)[1]);
        if (cmd != NULL) {
            CLIManager_displayCommandHelp(cmd);
        } else {
            CLIConsole_print("'%s%s%s' is unknown command name\n",
                             mgr->colorMap.attrCmdUnknown,
                             CLIInput_getArgV(&mgr->input)[1],
                             mgr->colorMap.attrDef);
        }
    }
}

static void CLIManager_displayPrompt(CLIManager_t* mgr)
{
    if (mgr->promptEnable == false) {
        return;
    }

    CLIConsole_print("%s%s> %s", mgr->colorMap.attrPrompt, mgr->prompt, mgr->colorMap.attrDef);
}

static void CLIManager_executeA(CLIManager_t* mgr)
{
    const char** argV = CLIInput_getArgV(&mgr->input);
    
    if (strcmp(argV[0], "menu") == 0) {
        CLIManager_displayMenu(mgr);
        return;
    }

    if (strcmp(argV[0], "help") == 0) {
        CLIManager_displayHelp(mgr);
        return;
    }

    ICLICommand_t* cmd = CLIRegistry_getCommand(&mgr->registry, argV[0]);
    if (cmd == NULL) {
        cmd = CLIRegistry_getCommand(&mgr->registryHidden, argV[0]);
        if (cmd == NULL) {
            CLIConsole_print("unknown command name: '%s%s%s'\n",
                             mgr->colorMap.attrCmdUnknown,
                             argV[0],
                             mgr->colorMap.attrDef);

            CLIConsole_print("use the '%smenu%s' command to get list of commands\n",
                             mgr->colorMap.attrCmdHelp,
                             mgr->colorMap.attrDef);

            return;
        }
    }
    
    #if CLI_CHECK_MEMORY_LEAK == 1
        if (strcmp(argV[0], "heap") != 0) {
            HeapMap_start();
            HeapMgr_capture(true);
        }
    #endif

    CLIInput_t input;
    CLIInput_init(&input);
    CLIInput_echoEnable(&input, CLIInput_echoEnabled(&mgr->input));

    cmd->execute(cmd, &input, CLIInput_getArgC(&mgr->input), (const char**)&argV[0]);

    CLIInput_deInit(&input);

    #if CLI_CHECK_MEMORY_LEAK == 1
        if (strcmp(argV[0], "heap") != 0) {
            HeapMap_stop();
            HeapMgr_capture(false);
            HeapMgr_diff();
        }
    #endif
}

static void CLIManager_execute(CLIManager_t* mgr)
{
    CLIManager_applyConsole(mgr);
    CLIManager_executeA(mgr);
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes CLI manager
 * 
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_init(CLIManager_t*               mgr,
                     CLIConsole_t*               console,
                     const CLIManagerColorMap_t* colorMap,
                     bool                        echoEnable)
{
    assert(mgr != NULL);
    assert(console != NULL);

    memset(mgr, 0, sizeof(CLIManager_t));

    mgr->console = console;

    if (colorMap == NULL) {
        mgr->colorMap.attrDef        = "";
        mgr->colorMap.attrPrompt     = "";
        mgr->colorMap.attrCmdUnknown = "";
        mgr->colorMap.attrCmdHelp    = "";
    } else {
        mgr->colorMap = *colorMap;
    }

    CLIManager_applyConsole(mgr);

    CLIInput_init(&mgr->input);
    CLIInput_echoEnable(&mgr->input, echoEnable);

    strcpy(mgr->prompt, "/");
}

/**
 * @brief Starts the CLI manager
 * 
 * @details It needs to be called after CLIManager_init
 * 
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_start(CLIManager_t* mgr)
{
    CLIManager_applyConsole(mgr);

    CLIManager_displayPrompt(mgr);
}

/**
 * @brief Sets new prompt string
 * 
 * @param mgr    Pointer to the CLIManager_t object
 * @param prompt Pointer to prompt string
 */
void CLIManager_setPrompt(CLIManager_t* mgr, const char* prompt)
{
    if (prompt != NULL) {
        strcpy(mgr->prompt, prompt);
        mgr->promptEnable = true;
    } else {
        mgr->promptEnable = false;
    }
}

/**
 * @brief Tries to execute a command from a string, and the user
 *        has the option to cancel the execution.
 * 
 * @param mgr     Pointer to the CLIManager_t object
 * @param cmd     Pointer to the string with command and/or parameters 
 * @param timeout The timeout value during which the user can 
 *                cancel the execution of this command.
 */
bool CLIManager_autoexecute(CLIManager_t* mgr, const char* cmd, uint32_t timeout)
{
    assert(cmd != NULL);
    assert(strlen(cmd) > 0);

    CLIManager_applyConsole(mgr);

    CLIConsole_print("run '%s%s%s'? (y/n)\n",
                     mgr->colorMap.attrCmdHelp,
                     cmd,
                     mgr->colorMap.attrDef);

    TimerSysMs_t t;
    TimerSysMs_startWithValue(&t, timeout);

    while (true) {
        if (TimerSysMs_check(&t) == true) {
            CLIManager_refreshCmd(mgr, cmd);
            return true;
        }

        if (CLIConsole_kbhit() == false) {
            continue;
        }

        char symbol = CLIConsole_getchar();

        if (symbol == 'n') {
            CLIConsole_print("\n");
            CLIManager_displayPrompt(mgr);
            return false;
        }
        
        if (symbol == 'y') {
            CLIConsole_print("\n");
            CLIManager_refreshCmd(mgr, cmd);
            return true;
        }
    }
}

/**
 * @brief Executes the command 
 * 
 * @param mgr Pointer to the CLIManager_t object
 * @param cmd Pointer to the string with command and/or parameters
 */
void CLIManager_refreshCmd(CLIManager_t* mgr, const char* cmd)
{
    CLIInput_pushCommand(&mgr->input, cmd);
    CLIManager_execute(mgr);
    CLIManager_reset(mgr);
}

/**
 * @brief Refreshes the CLI manager
 * 
 * @details It needs to be called periodically somewhere in the main loop
 * 
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_refresh(CLIManager_t* mgr)
{
    CLIManager_applyConsole(mgr);

    if (CLIInput_refresh(&mgr->input) == false) {
        return;
    }
    
    if (CLIInput_getArgC(&mgr->input) != 0) {
        CLIManager_execute(mgr);
    }

    CLIManager_reset(mgr);
}

/**
 * @brief Resets the CLI manager
 * 
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_reset(CLIManager_t* mgr)
{
    CLIManager_applyConsole(mgr);

    CLIInput_reset(&mgr->input);

    CLIManager_displayPrompt(mgr);
}

/**
 * @brief Registries new command
 * 
 * @param mgr Pointer to the CLIManager_t object
 * @param cmd Pointer to the new command
 */
void CLIManager_addCommand(CLIManager_t* mgr, ICLICommand_t* cmd)
{
    CLIRegistry_addCommand(&mgr->registry, cmd);
}

/**
 * @brief Registries new command
 *
 * @param mgr Pointer to the CLIManager_t object
 * @param cmd Pointer to the new command
 */
void CLIManager_addHiddenCommand(CLIManager_t* mgr, ICLICommand_t* cmd)
{
    CLIRegistry_addCommand(&mgr->registryHidden, cmd);
}

/**
 * @brief Clears the command list
 *
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_clearCommands(CLIManager_t* mgr)
{
    CLIRegistry_clear(&mgr->registry);
}

/**
 * @brief Sets its console as active
 *
 * @param mgr Pointer to the CLIManager_t object
 */
void CLIManager_applyConsole(CLIManager_t* mgr)
{
    CLIConsole_set(mgr->console);
}
