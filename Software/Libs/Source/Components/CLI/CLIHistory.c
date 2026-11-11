/**
 ******************************************************************************
 * @file    CLIConsole.c
 * @brief   This is a list of recent CLI_HISTORY_DEEP commands. Only unique
 *          commands are placed in the list.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */
/// Initializes the component to store the list of called commands

#include "CLI/CLIHistory.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
/// Define
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static int CLIHistory_cmdPresents(CLIHistory_t* history, const char* cmd)
{
    assert(history != NULL);

    int idx = 0;
    LinkedListNode_t* node = LinkedList_getRoot(&history->list);
    while (node != NULL) {
        if (strcmp(node->item, cmd) == 0) {
            return idx;
        }
        
        ++idx;
        node = node->next;
    }

    return -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes history of commands component
 * 
 * @param history Pointer to CLIHistory_t object
 */
void CLIHistory_init(CLIHistory_t* history)
{
    assert(history != NULL);

    history->index = 0;
    LinkedList_init(&history->list); 
}

/**
 * @brief Deinitializes history of commands component
 * 
 * @param history Pointer to CLIHistory_t object
 */
void CLIHistory_deInit(CLIHistory_t* history)
{
    assert(history != NULL);

    while (LinkedList_getCount(&history->list) != 0) {
        free((void*)LinkedList_getRoot(&history->list)->item);
        LinkedList_remove(&history->list, 0);
    }
}

/**
 * @brief Pushes the command to the list if it is unique
 * 
 * @param history Pointer to CLIHistory_t object
 * @param cmd     Pointer to string with command
 */
void CLIHistory_push(CLIHistory_t* history, const char* cmd)
{
    assert(history != NULL);

    history->index = 0;

    if (strlen(cmd) == 0) {
        return;
    }

    int presentPos = CLIHistory_cmdPresents(history, cmd);

    if (presentPos < 0) {
        if (LinkedList_getCount(&history->list) >= CLI_HISTORY_DEEP) {
            free((void*) LinkedList_getItem(&history->list, LinkedList_getCount(&history->list) - 1));
            LinkedList_remove(&history->list, LinkedList_getCount(&history->list) - 1);
        }

        char* cmdCopy = (char*) malloc(strlen(cmd) + 1);

        strcpy(cmdCopy, cmd);

        LinkedList_insItem(&history->list, 0, cmdCopy);
    } else if (presentPos > 0) {
        void* item = LinkedList_getItem(&history->list, presentPos);
        LinkedList_remove(&history->list, presentPos);
        LinkedList_insItem(&history->list, 0, item);
    }
}

/**
 * @brief Resets index to the command list
 * 
 * @param history Pointer to CLIHistory_t object
 */
void CLIHistory_resIndex(CLIHistory_t* history)
{
    assert(history != NULL);

    history->index = 0;
}

/**
 * @brief Increments index to the command list
 *
 * @param history Pointer to CLIHistory_t object
 */
void CLIHistory_incIndex(CLIHistory_t* history)
{
    assert(history != NULL);

    if ((history->index + 1) >= LinkedList_getCount(&history->list)) {
        return;
    }

    ++history->index;
}

/**
 * @brief Decrements index to the command list
 * 
 * @param history Pointer to CLIHistory_t object
 */
void CLIHistory_decIndex(CLIHistory_t* history)
{
    assert(history != NULL);

    if (history->index < 0) {
        return;
    }

    --history->index;
}

/**
 * @brief Returns current value of index
 * 
 * @param history Pointer to CLIHistory_t object
 * @return        The value of index
 */
int8_t CLIHistory_getIndex(CLIHistory_t* history)
{
    return history->index;
}

/**
 * @brief Returns the command pointed to by index
 * 
 * @param history Pointer to CLIHistory_t object
 * @return        Pointer to command
 */
const char* CLIHistory_getIndexCommand(CLIHistory_t* history)
{
    assert(history != NULL);

    if (LinkedList_getCount(&history->list) == 0) {
        return NULL;
    }

    return (const char*) LinkedList_getItem(&history->list, history->index);
}
