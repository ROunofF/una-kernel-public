#ifndef __CLIHistory_h__
#define __CLIHistory_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"
#include "CLIOptions.h"
#include "LinkedList/LinkedList.h"

////////////////////////////////////////////////////////////////////////////////
/// Define
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    int8_t index;

    LinkedList_t list;
} CLIHistory_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void        CLIHistory_init(CLIHistory_t* history);
void        CLIHistory_deInit(CLIHistory_t* history);
void        CLIHistory_push(CLIHistory_t* history, const char* cmd);
void        CLIHistory_resIndex(CLIHistory_t* history);
void        CLIHistory_incIndex(CLIHistory_t* history);
void        CLIHistory_decIndex(CLIHistory_t* history);
int8_t      CLIHistory_getIndex(CLIHistory_t* history);
const char* CLIHistory_getIndexCommand(CLIHistory_t* history);

#ifdef __cplusplus
 }
#endif

#endif
