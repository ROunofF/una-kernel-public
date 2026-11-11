#ifndef __CLIManager_h__
#define __CLIManager_h__

#include "CLI/ICommand.h"
#include "CLI/CLIRegistry.h"
#include "CLI/CLIInput.h"
#include "CLI/CLIConsole.h"

#ifdef __cplusplus
 extern "C" {
#endif

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    const char* attrDef;
    const char* attrPrompt;
    const char* attrCmdUnknown;
    const char* attrCmdHelp;
} CLIManagerColorMap_t;

typedef struct {
    CLIConsole_t*        console;
    CLIManagerColorMap_t colorMap;
    CLIRegistry_t        registry;
    CLIRegistry_t        registryHidden;
    CLIInput_t           input;
    char                 prompt[80];
    bool                 promptEnable;
    bool                 echoEnable;
} CLIManager_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void CLIManager_init(CLIManager_t*               mgr,
                     CLIConsole_t*               console,
                     const CLIManagerColorMap_t* colorMap,
                     bool                        echoEnable);
void CLIManager_start(CLIManager_t* mgr);
void CLIManager_setPrompt(CLIManager_t* mgr, const char* prompt);
bool CLIManager_autoexecute(CLIManager_t* mgr, const char* cmd, uint32_t timeout);
void CLIManager_refreshCmd(CLIManager_t* mgr, const char* cmd);
void CLIManager_refresh(CLIManager_t* mgr);
void CLIManager_reset(CLIManager_t* mgr);
void CLIManager_addCommand(CLIManager_t* mgr, ICLICommand_t* cmd);
void CLIManager_addHiddenCommand(CLIManager_t* mgr, ICLICommand_t* cmd);
void CLIManager_clearCommands(CLIManager_t* mgr);
void CLIManager_applyConsole(CLIManager_t* mgr);

#ifdef __cplusplus
 }
#endif

#endif
