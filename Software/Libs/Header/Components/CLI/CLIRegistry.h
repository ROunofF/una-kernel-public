#ifndef __CLIRegistry_h__
#define __CLIRegistry_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "CLIOptions.h"
#include "CLI/ICommand.h"

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
/// Data type
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    ICLICommand_t* command[CLI_REGISTRY_SIZE];
    uint8_t        count;
} CLIRegistry_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void           CLIRegistry_clear(CLIRegistry_t* r);
void           CLIRegistry_addCommand(CLIRegistry_t* r, ICLICommand_t* cmd);
ICLICommand_t* CLIRegistry_getCommand(CLIRegistry_t* r, const char* name);
uint8_t        CLIRegistry_getCount(CLIRegistry_t* r);
ICLICommand_t* CLIRegistry_getItem(CLIRegistry_t* r, uint8_t index);

#ifdef __cplusplus
 }
#endif

#endif
