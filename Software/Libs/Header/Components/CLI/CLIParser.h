#ifndef __CLIParser_h__
#define __CLIParser_h__

#include "Porting.h"

#ifdef __cplusplus
 extern "C" {
#endif

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    int          argC;
    const char** argV;
} CLIParser_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void        CLIParser_init(CLIParser_t* p, int argc, const char** argv);
bool        CLIParser_check(CLIParser_t* p, const char* optName);
bool        CLIParser_valueExists(CLIParser_t* p, const char* optName);
const char* CLIParser_getValueAsString(CLIParser_t* p, const char* optName);
bool        CLIParser_getValueAsUI32(CLIParser_t* p, const char* optName, uint32_t* value);
bool        CLIParser_getValueAsI32(CLIParser_t* p, const char* optName, int32_t* value);
bool        CLIParser_getValueAsFloat(CLIParser_t* p, const char* optName, float* value);

uint8_t     CLIParser_getElements(CLIParser_t* p, const char* optName);
const char* CLIParser_getElementAsString(CLIParser_t* p, const char* optName, uint8_t i);
bool        CLIParser_getElementAsUI32(CLIParser_t* p, const char* optName, uint8_t i, uint32_t* value);
bool        CLIParser_getElementAsI32(CLIParser_t* p, const char* optName, uint8_t i, int32_t* value);
bool        CLIParser_getElementAsFloat(CLIParser_t* p, const char* optName, uint8_t i, float* value);

#ifdef __cplusplus
 }
#endif

#endif
