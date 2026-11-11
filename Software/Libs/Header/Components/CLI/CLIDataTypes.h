#ifndef __CLIDataTypes_h__
#define __CLIDataTypes_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"
#include "CLIOptions.h"

typedef bool     (*CLIConsole_connected_t)();
typedef void     (*CLIConsole_send_t)(const uint8_t* buff, uint16_t len);
typedef uint16_t (*CLIConsole_getInQueue_t)(void);
typedef uint16_t (*CLIConsole_recv_t)(uint8_t* buff, uint16_t len);

#ifdef __cplusplus
 }
#endif

#endif
