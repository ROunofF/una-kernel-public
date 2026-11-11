#ifndef __ICLICommand_h__
#define __ICLICommand_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "CLIOptions.h"
#include "CLIInput.h"

typedef struct ICLICommandStruct ICLICommand_t;

typedef void        (*ICLICommad_execute)(ICLICommand_t* cmd, CLIInput_t* input, int argc, const char** argv);
typedef const char* (*ICLICommad_getName)(ICLICommand_t* cmd);
typedef const char* (*ICLICommad_getDesc)(ICLICommand_t* cmd);
typedef const char* (*ICLICommad_getHelp)(ICLICommand_t* cmd);

struct ICLICommandStruct {
    ICLICommad_execute execute;
    ICLICommad_getName getName;
    ICLICommad_getDesc getDesc;
    ICLICommad_getHelp getHelp;

    void* ctx;
};

#ifdef __cplusplus
 }
#endif

#endif
