/**
 ******************************************************************************
 * @file    CLIConsole.c
 * @brief   This is a list of commands that were registered by client code. 
 *          CLIManager can show this list by the menu command and provide help
 *          on each of them.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CLI/CLIRegistry.h"

#include <string.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Clears the command list
 *
 * @param r   Pointer to the registry object
 */
void CLIRegistry_clear(CLIRegistry_t* r)
{
    r->count = 0;
}

/**
 * @brief Adds the command to the registry
 * 
 * @param r   Pointer to the registry object
 * @param cmd Pointer to the command object
 */
void CLIRegistry_addCommand(CLIRegistry_t* r, ICLICommand_t* cmd)
{
    assert(r->count < CLI_REGISTRY_SIZE);

    r->command[r->count++] = cmd;
}

/**
 * @brief Returns command object by its name
 * 
 * @param r    Pointer to the registry object
 * @param name Command name
 * @return     Pointer to the command object
 */
ICLICommand_t* CLIRegistry_getCommand(CLIRegistry_t* r, const char* name)
{
    assert (name != NULL);

    for (uint8_t index = 0; index < r->count; index++) {
        ICLICommand_t* cmd = r->command[index];
        if (strcmp(cmd->getName(cmd), name) == 0) {
            return cmd;
        }
    }

    return NULL;
}

/**
 * @brief Returns number of commands
 * 
 * @param r Pointer to the registry object
 * @return  Number of commands in the registry
 */
uint8_t CLIRegistry_getCount(CLIRegistry_t* r)
{
    return r->count;
}

/**
 * @brief Returns pointer to command object by its index in the registry
 * 
 * @param r     Pointer to the registry object
 * @param index Command index in the registry
 * @return      Pointer to the command object
 */
ICLICommand_t* CLIRegistry_getItem(CLIRegistry_t* r, uint8_t index)
{
    assert(index < r->count);

    return r->command[index];
}
